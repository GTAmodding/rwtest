//////////// NB: for non-devkits these three have to be defined!!!
// this disconnects dsedb if run with -r run foo.elf
// but it works if you run from within dsedb
//#define REBOOT_IOP
//#define IOP_CDROM	// load IOP modules from CD
#define CDROM		// enable CD support

// load files from gta3.dir/img
#define USE_CDIMAGE

#include <ctype.h>
#include "rwcore.h"
#include "rpworld.h"

#ifdef RWMETRICS
#include "metrics.h"
#endif

#include "rtcharse.h"

#if (!defined(RWDEBUG))
#define  __RWRELEASEUNUSED__   __RWUNUSED__
#endif /* (!defined(RWDEBUG)) */

#if (!defined(__RWRELEASEUNUSED__))
#define  __RWRELEASEUNUSED__   /* No op */
#endif /* (!defined(__RWRELEASEUNUSED__)) */

#if (!defined(__GNUC__))
#define __FUNCTION__ "Unknown"
#endif /* (!defined(__GNUC__)) */


// skeleton stuff
#include "camera.h"
#include "skyfs.h"

// SCE stuff
#include "eeregs.h"
#include "sifdev.h"
#include "sifrpc.h"
#include "libpad.h"
#ifdef CDROM
#include "libcdvd.h"
#endif


#include "common.h"
#include "FileMgr.h"
#include "Pad.h"

#ifdef WIDE_SCREEN
#define DEFAULT_ASPECTRATIO (16.0f/9.0f)
#else
#define DEFAULT_ASPECTRATIO (4.0f/3.0f)
#endif

#define DEFAULT_VIEWWINDOW (0.5f)

static RwInt32 FrameCounter = 0;
static RwInt32 FramesPerSecond = 0;

static RwRGBA ForegroundColor = {200, 200, 200, 255};
static RwRGBA BackgroundColor = { 64,  64,  64,   0};

static RtCharset *Charset = NULL;
static RpLight *AmbientLight = NULL;
static RpLight *MainLight = NULL;

RpClump *Clump = NULL;
RpWorld *World = NULL;
RwCamera *Camera = NULL;
RwTexDictionary *TexDict = NULL;



void RsTerminate(void);
RwBool RsInitialise(void);
void RsRwTerminate(void);
RwBool RsRwInitialise(void);

RwUInt32 RsTimer(void);
void RsErrorMessage(const RwChar * message);

typedef struct RsGlobalType RsGlobalType;
struct RsGlobalType
{
	const RwChar *appName;
	RwInt32 maximumWidth;
	RwInt32 maximumHeight;
	RwInt32 maxFPS;
	RwBool  quit;

	void   *ps; /* platform specific data */
};
RsGlobalType        RsGlobal;



struct DirEntry
{
	uint32 offset;
	uint32 size;
	char name[24];
};
DirEntry directory[4000];
uint32 maxFileSize;
uint8 *streamBuffer;

int cdImageFD;

void
LoadDirectory(int fd)
{
	DirEntry de;
	int i = 0;
	while(CFileMgr::Read(fd, (char*)&de, 32) == 32){
//		printf("dir: %08X %08X %s\n", de.offset, de.size, de.name);
		directory[i++] = de;
		if(de.size > maxFileSize)
			maxFileSize = de.size;
	}
}
int
FindDirEntry(const char *name)
{
	int i;
	for(i = 0; i < 4000; i++){
		if(directory[i].offset == 0 &&
		   directory[i].size == 0)
			return -1;
		if(strcasecmp(directory[i].name, name) == 0)
			return i;
	}
	return -1;
}


static RpWorld *
CreateWorld(void)
{
	RpWorld *world;
	RwBBox bBox;

	bBox.sup.x = bBox.sup.y = bBox.sup.z = 100.0f;
	bBox.inf.x = bBox.inf.y = bBox.inf.z = -100.0f;

	world = RpWorldCreate(&bBox);

	return world;
}


static RpLight *
CreateAmbientLight(RpWorld *world)
{
	RpLight *light;

	light = RpLightCreate(rpLIGHTAMBIENT);
	if(light) {
		RwRGBAReal color;

		color.red = color.green = color.blue = 0.3f;
		color.alpha = 1.0f;

		RpLightSetColor(light, &color);
		RpWorldAddLight(world, light); 

		return light;
	}

	return NULL;
}


static RpLight *
CreateMainLight(RpWorld *world)
{
	RpLight *light;

	light = RpLightCreate(rpLIGHTDIRECTIONAL);
	if(light) {
		RwFrame *frame;

		frame = RwFrameCreate();
		if(frame) {
			RwV3d xAxis = {1.0f, 0.0f, 0.0f};
			RwV3d yAxis = {0.0f, 1.0f, 0.0f};

			RwFrameRotate(frame, &xAxis, 30.0f, rwCOMBINEREPLACE);
			RwFrameRotate(frame, &yAxis, 30.0f, rwCOMBINEPOSTCONCAT);

			RpLightSetFrame(light, frame);

			RpWorldAddLight(world, light); 

			return light;
		}

		RpLightDestroy(light);
	}

	return NULL;
}


static RwCamera *
CreateCamera(RpWorld *world)
{
	RwCamera *camera;

	camera = CameraCreate(RsGlobal.maximumWidth, RsGlobal.maximumHeight, TRUE);
	if(camera) {
		RwFrame *frame;
		RwV3d pos;

		frame = RwCameraGetFrame(camera);

		pos = *RwMatrixGetAt(RwFrameGetMatrix(frame));
		RwV3dScale(&pos, &pos, -5.0f);

		RwFrameTranslate(frame, &pos, rwCOMBINEREPLACE);

		RwCameraSetFarClipPlane(camera, 250.0f);
		RwCameraSetNearClipPlane(camera, 0.5f);

		RpWorldAddCamera(world, camera);

		return camera;
	}

	return NULL;
}

RwStream*
OpenFromDirectory(const char *name)
{
	int slot;
	RwMemory mem;
	
	slot = FindDirEntry(name);
	if(slot == -1){
		printf("couldn't find <%s>\n", name);
		return NULL;
	}
	printf("found <%s>: %d %X %X\n", name, slot, directory[slot].offset, directory[slot].size);
	CFileMgr::Seek(cdImageFD, directory[slot].offset*2048, 0);
	CFileMgr::Read(cdImageFD, (char*)streamBuffer, directory[slot].size*2048);
	mem.start = streamBuffer;
	mem.length = directory[slot].size*2048;
	return RwStreamOpen(rwSTREAMMEMORY, rwSTREAMREAD, &mem);
}

#ifdef USE_CDIMAGE
RwStream*
OpenStream(const char *name)
{
	return OpenFromDirectory(name);
}
#else
RwStream*
OpenStream(const char *name)
{
	char path[256];
	sprintf(path, "./models/%s", name);
	return RwStreamOpen(rwSTREAMFILENAME, rwSTREAMREAD, path);
}
#endif

static RpClump *
LoadClump(const RwChar *file)
{
	RpClump *clump = NULL;
	RwStream *stream;

	stream = OpenStream(file);
	if(stream) {
		if(RwStreamFindChunk(stream, rwID_CLUMP, NULL, NULL)) 
			clump = RpClumpStreamRead(stream);

		RwStreamClose(stream, NULL);
	}

	return clump;
}


static RpClump *
CreateClump(RpWorld *world)
{
	RpClump *clump;

	clump = LoadClump("player.dff");
	if( clump == NULL )
		return NULL;

	RpWorldAddClump(world, clump);

	return clump;
}

static RwTexDictionary*
LoadTextureDictionary(const char *file)
{
	RwTexDictionary *texDict = NULL;
	RwStream *stream;

	stream = OpenStream(file);
	if(stream) {
		if(RwStreamFindChunk(stream, rwID_TEXDICTIONARY, NULL, NULL))
			texDict = RwTexDictionaryStreamRead(stream);

		RwStreamClose(stream, NULL);
	}

	return texDict;
};

static RwBool 
Initialise(void)
{
	if(RsInitialise()) {
		RsGlobal.appName = "GTA test";
		RsGlobal.maxFPS = 120;

		return TRUE;
	}

	return FALSE;
}


uint8 buf[24*1024];

void
InitDirectory(void)
{
	int fd;

	fd = CFileMgr::OpenFile("cdrom0:\\MODELS\\GTA3.DIR");
	if(fd == 0){
		printf("error opening\n");
		return;
	}
	LoadDirectory(fd);
	CFileMgr::CloseFile(fd);
	streamBuffer = (uint8*)RwMalloc(maxFileSize*2048);

	cdImageFD = CFileMgr::OpenFile("cdrom0:\\MODELS\\GTA3.IMG");
	if(cdImageFD == 0){
		printf("error opening\n");
		return;
	}
}

RwBool 
Initialise3D(void)
{
	if(!RsRwInitialise()) {
		RsErrorMessage("Error initializing RenderWare.");
		return FALSE;
	}

	InitDirectory();

	Charset = RtCharsetCreate(&ForegroundColor, &BackgroundColor);
	if(Charset == NULL) {
		RsErrorMessage("Cannot create raster charset.");
		return FALSE;
	}

	World = CreateWorld();
	if(World == NULL) {
		RsErrorMessage("Cannot create world.");
		return FALSE;
	}

	AmbientLight = CreateAmbientLight(World);
	if(AmbientLight == NULL) {
		RsErrorMessage("Cannot create ambient light.");
		return FALSE;
	}

	MainLight = CreateMainLight(World);
	if(MainLight == NULL) {
		RsErrorMessage("Cannot create main light.");
		return FALSE;
	}

	Camera = CreateCamera(World);
	if(Camera == NULL) {
		RsErrorMessage("Cannot create camera.");
		return FALSE;
	}

	TexDict = LoadTextureDictionary("player.txd");
	if(TexDict)
		RwTexDictionarySetCurrent(TexDict);

	Clump = CreateClump(World);
	if(Clump == NULL) {
		RsErrorMessage("Cannot create clump.");
		return FALSE;
	}

#ifdef RWMETRICS
	RsMetricsOpen(Camera);
#endif

	return TRUE;
}


static void 
Terminate3D(void)
{
#ifdef RWMETRICS
	RsMetricsClose();
#endif

	if(Clump) {
		RpWorldRemoveClump(World, Clump);
		RpClumpDestroy(Clump);
	}

	if(Camera) {
		RpWorldRemoveCamera(World, Camera);

		CameraDestroy(Camera);
	}

	if(AmbientLight) {
		RpWorldRemoveLight(World, AmbientLight);

		RpLightDestroy(AmbientLight);
	}

	if(MainLight) {
		RwFrame *frame;

		RpWorldRemoveLight(World, MainLight);

		frame = RpLightGetFrame(MainLight);
		RpLightSetFrame(MainLight, NULL);
		RwFrameDestroy(frame);

		RpLightDestroy(MainLight);
	}

	if(World)
		RpWorldDestroy(World);

	if(Charset)
		RwRasterDestroy(Charset);

	RsRwTerminate();
}


static RwBool 
AttachPlugins(void)
{
	if(!RpWorldPluginAttach())
		return FALSE;
	return TRUE;
}

void 
FrameRotate(RwReal xAngle, RwReal yAngle)
{
	RwFrame *frame;
	RwV3d *right, *up;
	RwV3d pos;

	frame = RpClumpGetFrame(Clump);

	right = RwMatrixGetRight(RwFrameGetMatrix(RwCameraGetFrame(Camera)));
	up = RwMatrixGetUp(RwFrameGetMatrix(RwCameraGetFrame(Camera)));

	pos = *RwMatrixGetPos(RwFrameGetMatrix(frame));

	RwV3dScale(&pos, &pos, -1.0f);
	RwFrameTranslate(frame, &pos, rwCOMBINEPOSTCONCAT);

	RwFrameRotate(frame, up, xAngle, rwCOMBINEPOSTCONCAT);
	RwFrameRotate(frame, right, yAngle, rwCOMBINEPOSTCONCAT);

	RwV3dScale(&pos, &pos, -1.0f);
	RwFrameTranslate(frame, &pos, rwCOMBINEPOSTCONCAT);
}

static void 
Render(void)
{
	RwCameraClear(Camera, &BackgroundColor, rwCAMERACLEARZ|rwCAMERACLEARIMAGE);

	if(RwCameraBeginUpdate(Camera)) {
		RpWorldRender(World);

#ifdef RWMETRICS
		RsMetricsRender();
#endif

		RwCameraEndUpdate(Camera);
	}

	RwCameraShowRaster(Camera, NULL, 0);

	FrameCounter++;
}


static void 
Idle(void)
{
	RwUInt32 thisTime;

	static RwBool firstCall = TRUE;
	static RwUInt32 lastFrameTime;

	if(firstCall) {
		lastFrameTime = RsTimer();
		firstCall = FALSE;
	}

	thisTime = RsTimer();

	if(thisTime > (lastFrameTime + 1000)) {
		FramesPerSecond = FrameCounter;
		FrameCounter = 0;
		lastFrameTime = thisTime;
	}


	float rotx = (pad1.NewState.DPadRight - pad1.NewState.DPadLeft)/255.0f;
	float roty = (pad1.NewState.DPadUp - pad1.NewState.DPadDown)/255.0f;
	FrameRotate(rotx*2.0f, roty*2.0f);

	rotx = pad1.NewState.LeftStickX/128.0f;
	roty = pad1.NewState.LeftStickY/128.0f;
	FrameRotate(rotx*2.0f, roty*2.0f);

	Render();
}





volatile unsigned long sweHighCount;
int                 skyTimerHandlerHid;


static int
TimerHandler(int ca)
{
	if ((ca == INTC_TIM0) && (*T0_MODE & 0x800)) {
		*T0_MODE |= 0x800;

		sweHighCount += 0x10000;
	}

	ExitHandler();

	return 0;
}

void
RsErrorMessage(const RwChar * message __RWRELEASEUNUSED__)
{
	RwDebugSendMessage(rwDEBUGERROR, __FUNCTION__, message);
}

void
RsWarningMessage(const RwChar * message __RWRELEASEUNUSED__)
{
	RwDebugSendMessage(rwDEBUGMESSAGE, __FUNCTION__, message);
}

void
psDebugMessageHandler(RwDebugType type, const RwChar * str)
{
	switch (type) {
#if (defined(COLOR))
	case rwDEBUGASSERT:   /* red */
		printf("\033[31m%s\033[0m\n", str);
		break;
	case rwDEBUGERROR:    /* bold red */
		printf("\033[31;1m%s\033[0m\n", str);
		break;
	case rwDEBUGMESSAGE:  /* blue */
		printf("\033[34m%s\033[0m\n", str);
		break;
#endif
	default:
		printf("%s\n", str);
    }
}

RwUInt32
RsTimer(void)
{
	unsigned long       high0, low0, high1, low1;

	high0 = sweHighCount;
	low0 = *T0_COUNT;
	high1 = sweHighCount;
	low1 = *T0_COUNT;

	if (high0 == high1)
		return ((RwUInt32) ((high0 | (low0 & 0xffff)) / 9216));
	else
		return ((RwUInt32) ((high1 | (low1 & 0xffff)) / 9216));
}

RwUInt64
psMicroTimer(void)
{
	unsigned long       high0, low0, high1, low1;

	high0 = sweHighCount;
	low0 = *T0_COUNT;
	high1 = sweHighCount;
	low1 = *T0_COUNT;

	if (high0 == high1)
		return (((RwUInt64) (high0 | (low0 & 0xffff))) * 1000) / 9216;
	else
		return (((RwUInt64) (high1 | (low1 & 0xffff))) * 1000) / 9216;
}


RwBool
RsSelectDevice(void)
{
	RwVideoMode         vmodeInfo;
	RwInt32             numVideoModes;
	RwInt32             i;
	RwVideoMode         videoMode;

	numVideoModes = RwEngineGetNumVideoModes() + 1;

	for (i = 1; i < numVideoModes; i++)
		if (RwEngineGetVideoModeInfo(&vmodeInfo, (i - 1))) {
			if(vmodeInfo.width == 640 && vmodeInfo.height == 512 &&
			   vmodeInfo.depth == 32 &&
			   vmodeInfo.flags == (rwVIDEOMODEEXCLUSIVE|rwVIDEOMODEINTERLACE|rwVIDEOMODEFSAA1)) {
				RwEngineSetVideoMode(i-1);
				break;
			}
		}
	RwEngineGetVideoModeInfo(&videoMode, RwEngineGetCurrentVideoMode());

	RsGlobal.maximumWidth = videoMode.width;
	RsGlobal.maximumHeight = videoMode.height;

	return TRUE;
}

void
RsTerminate(void)
{
}

RwBool
RsInitialise(void)
{
	RwBool              result;

	RsGlobal.appName = "RenderWare Application";
	RsGlobal.maximumWidth = 0;
	RsGlobal.maximumHeight = 0;
	RsGlobal.maxFPS = 0;
	RsGlobal.quit = FALSE;

	return TRUE;
}

// this is where the magic happens
char*
GetModulePath(const char *module)
{
	char tmp[64];
	static char path[256];
	char *s;

	strcpy(tmp, module);
#ifdef IOP_CDROM
	strcpy(path, "cdrom0:\\SYSTEM\\");
#else
#define TOSTRING__(arg) #arg
#define TOSTRING(arg) TOSTRING__(arg)
	for(s = tmp; *s; s++)
		if(isupper(*s)) *s = tolower(*s);
	sprintf(path, "host0:%s/modules/", TOSTRING(IOPPATH));
#endif
	strcat(path, tmp);
	return path;
}

void
LoadModule(const char *module)
{
	static char noargs[] = "";
	char *path;
	path = GetModulePath(module);
	printf("Loading %s\n", path);
	while(sceSifLoadModule(path, 0, noargs) < 0)
		printf("Can't Load Module %s\n", path);
}

void
TheGame(void)
{
	while (!RsGlobal.quit) {
		static RwUInt32     lastpoll = 0;
		RwUInt32            polltime;

		polltime = RsTimer();
		if (polltime - lastpoll > 15) {
			UpdatePad(&pad1, 0);

			lastpoll = polltime;
		}

		Idle();
	}
}

void
SystemInit(void)
{
	skyTimerHandlerHid = -1;

	sceSifInitRpc(0);
	sceSifInitIopHeap();

#ifdef CDROM
	CFileMgr::InitCdSystem();
#endif

#ifdef REBOOT_IOP
	printf("rebooting IOP\n");
	while(!sceSifRebootIop(GetModulePath(IOP_IMAGE_FILE)))
		continue;

	while(!sceSifSyncIop())
		continue;

	sceSifInitRpc(0);

#ifdef CDROM
	CFileMgr::InitCdSystem();
#endif
	sceFsReset();
#endif

	LoadModule("SIO2MAN.IRX");
	LoadModule("PADMAN.IRX");

	skyTimerHandlerHid = AddIntcHandler(INTC_TIM0, TimerHandler, 0);
	/* Set up time0 */
	sweHighCount = 0;
	*T0_COUNT = 0;
	*T0_COMP = 0;
	*T0_HOLD = 0;
	*T0_MODE = 0x281;
	EnableIntc(INTC_TIM0);
}

void
GameInit(void)
{
	RwRect              r;

	Initialise3D();

	r.x = 0;
	r.y = 0;
	r.w = RsGlobal.maximumWidth;
	r.h = RsGlobal.maximumHeight;
	CameraSize(Camera, &r, DEFAULT_VIEWWINDOW, DEFAULT_ASPECTRATIO);
}

int
main(int argc, char *argv[])
{
	SystemInit();

	Initialise();	// not in GTA

	PadInit();

	GameInit();

	TheGame();

	Terminate3D();
	RsTerminate();

	DisableIntc(INTC_TIM0);
	RemoveIntcHandler(INTC_TIM0, skyTimerHandlerHid);

	return 0;
}


#ifdef RWDEBUG
static              RwBool
RsSetDebug(void)
{
	RwDebugSetHandler(psDebugMessageHandler);

	RwDebugSendMessage(rwDEBUGMESSAGE, RsGlobal.appName,
		       "Debugging Initialized");

	return TRUE;
}
#endif

static RwTexDictionary *
texDictDestroyCB(RwTexDictionary *dict, void *data __RWUNUSED__)
{
	RwTexDictionaryDestroy(dict);
	return dict;
}

void
RsRwTerminate(void)
{
	RtCharsetClose();

	RwTexDictionaryForAllTexDictionaries(texDictDestroyCB, NULL);

	RwEngineStop();
	RwEngineClose();
	RwEngineTerm();

	return;
}

RwBool
RsRwInitialise(void)
{
	RwEngineOpenParams  openParams;

	if (!RwEngineInit(NULL, 0))
		return (FALSE);

	SkyInstallFileSystem();

#ifdef RWDEBUG
	RsSetDebug();
#endif

	if(!AttachPlugins()) {
		printf("Couldn't load the plugins for some reason\n");
		for(;;);
	}

	openParams.displayID = NULL;
	if (!RwEngineOpen(&openParams)) {
		RwEngineTerm();
		return (FALSE);
	}

	RsSelectDevice();

	if (!RwEngineStart()) {
		RwEngineClose();
		RwEngineTerm();
		return (FALSE);
	}

	if (!RtCharsetOpen()) {
		RwEngineStop();
		RwEngineClose();
		RwEngineTerm();
		return (FALSE);
	}

	return TRUE;
}

