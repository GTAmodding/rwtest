#include "stdio.h"
#include "string.h"
#include "ctype.h"
#include "assert.h"

/*#include "unistd.h"*/
#include "sifdev.h"
#include "libcdvd.h"

#include "rwcore.h"
#include "skyfs.h"

#define READBUFFERSIZE 16*1024

#define RWGDFSGLOBAL(var)                               \
    (RWPLUGINOFFSET(gdfsGlobals,                        \
                    RwEngineInstance,                   \
                    gdfsModuleInfo.globalsOffset)->var)

#define rwID_SKYDEVICEMODULE (MAKECHUNKID(rwVENDORID_CRITERIONINT, 0x30))

typedef struct skyFile skyFile;
struct skyFile
{
    RwUInt8             readBuffer[READBUFFERSIZE];
    int                 gdfs;
    RwInt32             POS;
    RwInt32             SOF;
    RwUInt32            bufferPos;
    RwBool              bufferValid;
};

typedef struct gdfsGlobals gdfsGlobals;
struct gdfsGlobals
{
    RwFileFunctions     oldFS;  /* Old Filing system vectors */
    RwFreeList         *fpFreeList; /* File pointer free list */
};

/* These are all global to all RenderWare instances */

RwInt32             gdfsOpenFiles;
RwModuleInfo        gdfsModuleInfo;

char gpDirectory[64];
#define MAX_FILE_INFOS 90
struct {
	char path[40];
	RwUInt32 pos;
	RwUInt32 size;
} cdFileInfoArray[MAX_FILE_INFOS];
int numFileInfos;

void
SkyRegisterFileOnCd(const char *file)
{
	sceCdlFILE cdfile;

	if(numFileInfos == 90){
		// not in release:
		scePrintf("no more file infos\n");
		return;
	}

	for(;;){
		sceCdDiskReady(0);
		if(sceCdSearchFile(&cdfile, file))
			break;
		scePrintf("Cannot find %s on CD\n", file);
	}
	cdFileInfoArray[numFileInfos].pos = cdfile.lsn;
	cdFileInfoArray[numFileInfos].size = cdfile.size;
	strcpy(cdFileInfoArray[numFileInfos].path, file);
scePrintf("registered: <%s> %d %d %d\n", file, cdfile.lsn, cdfile.size, cdfile.lsn+cdfile.size);
	numFileInfos++;
}

void
SkySetDirectory(const char *dir)
{
	strcpy(gpDirectory, dir);
}

// NB: only for cdrom!
void
SkyPostProcessFilename(char *buf, const char *file)
{
	int i;
	char *p;

	strcpy(buf, gpDirectory);
	strcat(buf, file);

	i = strlen(buf);
	p = buf + i;
	while(i--){
		if(islower(*p))
			*p = toupper(*p);
		else if(*p == '/')
			*p = '\\';
		else if(*p == ':')
			break;
		p--;
	}

	strcat(buf, ";1");
}

/****************************************************************************
 skyTransMode

 Attempt to convert a mode string to an open mode

 On entry  : access mode
 On exit   : integer mode
 */
static int
skyTransMode(const RwChar * access)
{
    int                 mode;
    char               *r;
    char               *w;
    char               *a;
    char               *plus;
    char               *n;
    char               *d;

    /* I add a couple of new characters for now:
     * n non-blocking mode
     * d no write back d cache */

    r = strrchr(access, (int) 'r');
    w = strrchr(access, (int) 'w');
    a = strrchr(access, (int) 'a');
    plus = strrchr(access, (int) '+');
    n = strrchr(access, (int) 'n');
    d = strrchr(access, (int) 'd');

    if (plus)
        mode = SCE_RDWR;
    else if (r)
        mode = SCE_RDONLY;
    else if (w)
        mode = SCE_WRONLY;
    else if (a)
        mode = SCE_WRONLY;
    else
        return (0);

    /* later we will test for SCE_CREAT & !SCE_TRUNC as a seek to end of file */
    if (w)
        mode |= SCE_CREAT | SCE_TRUNC;

    if (a)
        mode |= SCE_CREAT;

    if (n)
        mode |= SCE_NOWAIT;

    if (d)
        mode |= SCE_NOWBDC;

    return (mode);
}

static void *
trySkyFopen(const RwChar * fname, const RwChar * access)
{
	skyFile *fp;
	int      mode;
	RwChar   name[256];
	RwChar  *nameptr;
	int cdrom = 0;

	if (strchr(fname, ':')) {
		strncpy(name, fname, 255);
		if(strncmp(name, "cdrom", 5) == 0)
			cdrom = 1;
	} else {
#ifdef CDROM
		int i;
		int found;
		RwUInt32 pos, size;

		cdrom = 1;
		strcpy(name, "cdrom0:");
		nameptr = name+7;
		SkyPostProcessFilename(nameptr, fname);
		printf("<%s> -> <%s>\n", fname, name);

		found = 0;
		for(i = 0; !found && i < numFileInfos; i++){
			if(strcmp(cdFileInfoArray[i].path, nameptr) == 0){
				found = 1;
				pos = cdFileInfoArray[i].pos;
				size = cdFileInfoArray[i].size;
			}
		}

		if(found){
			printf("found registered file!\n");
		}else{
			printf("did not find registered file!\n");
			sceCdlFILE cdfile;
			for(;;){
				sceCdDiskReady(0);
				if(sceCdSearchFile(&cdfile, nameptr))
					break;
				scePrintf("Cannot find %s on CD\n", nameptr);
			}
			printf("now i got it\n");
		}
#else
		sprintf(name, "host0:%s", fname);
#endif
    }
    /* force null termination */
    name[255] = 0;

    nameptr = name;
    while (*nameptr)
    {
	if(cdrom){
		if (*nameptr == '/')
		    *nameptr = '\\';
	}else{
		if (*nameptr == '\\')
		    *nameptr = '/';
	}
        nameptr++;
    }

    /* Allocate structure for holding info */
    fp = (skyFile *) RwFreeListAlloc(RWGDFSGLOBAL(fpFreeList));
    if (!fp)
    {
        return (NULL);
    }

    mode = skyTransMode(access);
    if (!mode)
    {
        return (NULL);
    }

    fp->gdfs = sceOpen(name, mode);
printf("open <%s> -> %d\n", name, fp->gdfs);

    if (fp->gdfs < 0)
    {
        RwFreeListFree(RWGDFSGLOBAL(fpFreeList), fp);
        return (NULL);
    }

    /* We seek to the end of the file to get size */
    fp->SOF = fp->POS = sceLseek(fp->gdfs, 0, SCE_SEEK_END);
    if (fp->SOF < 0)
    {
        sceClose(fp->gdfs);
        RwFreeListFree(RWGDFSGLOBAL(fpFreeList), fp);
        return (NULL);
    }
    /* SCE_CREAT & !SCE_TRUNC mean seek to end of file */
    if (!((mode & SCE_CREAT) && !(mode & SCE_TRUNC)))
    {
        fp->POS = sceLseek(fp->gdfs, 0, SCE_SEEK_SET);

        if (fp->POS < 0)
        {
            sceClose(fp->gdfs);
            RwFreeListFree(RWGDFSGLOBAL(fpFreeList), fp);
            return (NULL);
        }
    }

    /* Initialise the buffer to show nothing buffered */
    fp->bufferPos = READBUFFERSIZE;

    gdfsOpenFiles++;

    return ((void *) fp);
}

/****************************************************************************
 skyFopen

 On entry   : Filename, access mode
 On exit    :
 */

static void        *
skyFopen(const RwChar * name, const RwChar * access)
{
	void               *res;

	res = trySkyFopen(name, access);

	if (res) {
		scePrintf("Opening %s\n", name);
		return (res);
	}

	scePrintf("Failed to open %s\n", name);
	return (NULL);
}

/****************************************************************************
 skyFclose

 On entry   : FILE * (data block specific to this implementation)
 On exit    : 0 on success
 */

static int
skyFclose(void *fptr)
{
    skyFile            *fp = (skyFile *) fptr;

    assert(fptr);

    if (fp && gdfsOpenFiles)
    {
        sceClose(fp->gdfs);

        RwFreeListFree(RWGDFSGLOBAL(fpFreeList), fp);

        --gdfsOpenFiles;

        return (0);
    }

    return (-1);
}

/****************************************************************************
 skyFexist

 On entry   : Filename
 On exit    :
 */

static              RwBool
skyFexist(const RwChar * name)
{
    void               *res;

    res = RwOsGetFileInterface()->rwfopen(name, "r");
    if (res)
    {
        RwOsGetFileInterface()->rwfclose(res);
        return (TRUE);
    }

    return (FALSE);
}

/****************************************************************************
 skyFread

 On entry   : Address to read to, block size, block count, file to read from
 On exit    : Number of bytes read
 */

static              size_t
skyFread(void *addr, size_t size, size_t count, void *fptr)
{
    skyFile            *fp = (skyFile *) fptr;
    size_t              numBytesToRead = size * count;
    int                 bytesRead, bytesRead2;

    bytesRead = 0;

    /* Trim number of bytes for the size of the file */
    if ((fp->POS + (RwInt32) numBytesToRead) > fp->SOF)
    {
        numBytesToRead = fp->SOF - fp->POS;
    }

    /* First try and use the buffer */
    if ((fp->bufferPos < READBUFFERSIZE) &&
        (bytesRead < (RwInt32) numBytesToRead))
    {
        /* Pull from the buffer */
        if (numBytesToRead < (READBUFFERSIZE - fp->bufferPos))
        {
            /* Can satisfy entirely from buffer */
            bytesRead = numBytesToRead;
        }
        else
        {
            /* Pull as much as possible from the buffer */
            bytesRead = READBUFFERSIZE - fp->bufferPos;
        }

        /* Copy it */
        memcpy(addr, &fp->readBuffer[fp->bufferPos], bytesRead);

        /* Update target address and source address */
        addr = (void *) ((RwUInt8 *) addr + bytesRead);
        fp->bufferPos += bytesRead;
        fp->POS += bytesRead;
    }

    /* If next bit is bigger than a buffer, read it directly and ignore the
     * buffer.
     */
    if ((numBytesToRead - bytesRead) > 0)
    {
        if ((numBytesToRead - bytesRead) >= READBUFFERSIZE)
        {
            bytesRead2 = (numBytesToRead - bytesRead);
            bytesRead2 = sceRead(fp->gdfs, addr, bytesRead2);
            if (bytesRead2 < 0)
            {
                bytesRead2 = 0;
            }
        }
        else
        {
            /* Go via the buffer */
            sceRead(fp->gdfs, fp->readBuffer, READBUFFERSIZE);
            bytesRead2 = (numBytesToRead - bytesRead);
            memcpy(addr, fp->readBuffer, bytesRead2);
            fp->bufferPos = bytesRead2;
        }
        fp->POS += bytesRead2;
        bytesRead += bytesRead2;
    }

    return (bytesRead / size);
}

/****************************************************************************
 skyFwrite

 On entry   : Address to write from, block size, block count, file to write to
 On exit    : Number of bytes written
 */

static              size_t
skyFwrite(const void *addr, size_t size, size_t count, void *fptr)
{
    int                 bytesWritten;
    skyFile            *fp = (skyFile *) fptr;
    RwInt32             numBytesToWrite = size * count;

    assert(addr);
    assert(fptr);

    bytesWritten = sceWrite(fp->gdfs, (void *)addr, numBytesToWrite);

    if (-1 != bytesWritten)
    {
        fp->POS += bytesWritten;
        if (fp->POS > fp->SOF)
            fp->SOF = fp->POS;
        return (size > 0 ? bytesWritten / size : 0);
    }
    return (0);
}

/****************************************************************************
 skyFseek

 On entry   : file to seek in, offset, how to seek
 On exit    : 0 on success
 */

static int
skyFseek(void *fptr, long offset, int origin)
{
    skyFile            *fp = (skyFile *) fptr;
    RwInt32             oldFPos, bufStart;
    RwBool              noBuffer = FALSE;

    assert(fptr);

    oldFPos = fp->POS;
    bufStart = oldFPos - fp->bufferPos;
    if (fp->bufferPos == READBUFFERSIZE)
        noBuffer = TRUE;
    fp->bufferPos = READBUFFERSIZE;

    switch (origin)
    {
        case SEEK_CUR:
            {
                /* Does the seek stay in the buffer */
                if (((oldFPos + offset >= bufStart) &&
                     (oldFPos + offset < bufStart + READBUFFERSIZE)) &&
                    (!noBuffer))
                {
                    fp->bufferPos = (oldFPos + offset) - bufStart;
                    fp->POS += offset;
                }
                else
                {
                    fp->POS =
                        sceLseek(fp->gdfs, oldFPos + offset,
                                 SCE_SEEK_SET);
                }
                break;
            }
        case SEEK_END:
            {
                fp->POS = sceLseek(fp->gdfs, offset, SCE_SEEK_END);
                break;
            }
        case SEEK_SET:
            {
                fp->POS = sceLseek(fp->gdfs, offset, SCE_SEEK_SET);
                break;
            }
        default:
            {
                return (-1);
            }
    }

    if (noBuffer)
        fp->bufferPos = READBUFFERSIZE;

    if (fp->POS == -1)
    {
        /* This may not be valid */
        fp->POS = oldFPos;
        fp->bufferPos = READBUFFERSIZE;
        return (-1);
    }

    return (0);
}

/****************************************************************************
 skyFtell

 On entry   : file to tell offset in
 On exit    : current offset in file
 */

static int
skyFtell(void *fptr)
{
    skyFile            *fp = (skyFile *) fptr;

    assert(fptr);

    return sceLseek(fp->gdfs, 0, SCE_SEEK_CUR);
}

/****************************************************************************
 skyFgets

 On entry   : Buffer to read into, max chars to read, file to read from
 On exit    : Returns pointer to string on success
 */

static RwChar      *
skyFgets(RwChar * buffer, int maxLen, void *fptr)
{
    skyFile            *fp = (skyFile *) fptr;
    RwInt32             i;
    RwInt32             numBytesRead;

    assert(buffer);
    assert(fptr);

    i = 0;

    numBytesRead = skyFread(buffer, 1, maxLen - 1, fp);

    if (numBytesRead == 0)
    {
        return (0);
    }

    while (i < numBytesRead)
    {
        if (buffer[i] == '\n')
        {
            i++;

            buffer[i] = '\0';

            /* the file pointer needs */
            /* to be reset as skyFread */
            /* will have overshot the */
            /* first new line         */

            i -= numBytesRead;
            skyFseek(fp, i, SEEK_CUR);

            return (buffer);
        }
        else if (buffer[i] == 0x0D)
        {
            if ((i < (numBytesRead - 1)) && (buffer[i + 1] == '\n'))
            {
                memcpy(&buffer[i], &buffer[i + 1],
                       (numBytesRead - i - 1));
                numBytesRead--;
            }
            else
                i++;
        }
        else
            i++;
    }

    /*
     * Don't return NULL because we could have read maxLen bytes
     * without finding a \n
     */
    return (buffer);
}

/****************************************************************************
 skyFputs

 On entry   : Buffer to write from, file to write to
 On exit    : Non negative value on success
 */

static int
skyFputs(const RwChar * buffer, void *fptr)
{
    skyFile            *fp = (skyFile *) fptr;
    int                 i, j;

    assert(buffer);
    assert(fptr);

    i = strlen(buffer);
    j = sceWrite(fp->gdfs, (void *) buffer, i);

    if (j != -1)
    {
        fp->POS += j;
        if (fp->POS > fp->SOF)
            fp->SOF = fp->POS;
    }
    if ((j == -1) || (i != j))
    {
        return (EOF);
    }
    return (j);
}

/****************************************************************************
 skyFeof

 On entry   : File to test for end of
 On exit    : Non zero if end of file reached
 */

static int
skyFeof(void *fptr)
{
    skyFile            *fp = (skyFile *) fptr;

    assert(fptr);

    return (fp->POS >= fp->SOF);
}

/****************************************************************************
 skyFflush

 On entry   :
 On exit    :
 */

static int
skyFflush(void *fptr __RWUNUSED__)
{
    return (0);
}

/****************************************************************************
 _rwSkyFSOpen

 On entry   : Instance, offset, size
 On exit    : instance pointer on success
 */

static void        *
_rwSkyFSOpen(void *instance, RwInt32 offset, RwInt32 size __RWUNUSED__)
{
    RwFileFunctions    *FS;

    /* Cache the globals offset */
    gdfsModuleInfo.globalsOffset = offset;

    /* Create a free list for file handle structures */
    RWGDFSGLOBAL(fpFreeList) = RwFreeListCreate(sizeof(skyFile), 5, 0);
    if (!RWGDFSGLOBAL(fpFreeList))
    {
        return (NULL);
    }

    /* This is per instance of RenderWare */
    FS = RwOsGetFileInterface();

    /* save away the old filing system */
    RWGDFSGLOBAL(oldFS) = *FS;

    /* attach the new filing system */
    FS->rwfexist = skyFexist;  /* FS->rwfexist;  */
    FS->rwfopen = skyFopen;
    FS->rwfclose = skyFclose;
    FS->rwfread = skyFread;
    FS->rwfwrite = skyFwrite;
    FS->rwfgets = skyFgets;
    FS->rwfputs = skyFputs;
    FS->rwfeof = skyFeof;
    FS->rwfseek = skyFseek;
    FS->rwfflush = skyFflush;
    FS->rwftell = skyFtell;

    gdfsModuleInfo.numInstances++;

    return (instance);
}

/****************************************************************************
 _rwSkyFSClose

 On entry   : instance, offset, size
 On exit    : instance pointer on success
 */

static void        *
_rwSkyFSClose(void *instance, RwInt32 offset __RWUNUSED__,
              RwInt32 size __RWUNUSED__)
{
    RwFileFunctions    *FS;

    FS = RwOsGetFileInterface();

    /* re-attach the old filing system - not strictly necessary,
     * but we are feeling kind today! */
    *FS = RWGDFSGLOBAL(oldFS);

    /* Blow away our free list */
    RwFreeListDestroy(RWGDFSGLOBAL(fpFreeList));

    gdfsModuleInfo.numInstances--;

    return (instance);
}

/****************************************************************************
 RwSkyInstallFileSystem

 On entry   :
 On exit    : TRUE on success
 */

RwBool
SkyInstallFileSystem(void)
{

//    gdfsOpenFiles = 0;

    if (RwEngineRegisterPlugin
        (sizeof(gdfsGlobals), rwID_SKYDEVICEMODULE, _rwSkyFSOpen,
         _rwSkyFSClose) < 0)
    {
        /* If it is negative, we've failed */
        return (FALSE);
    }

    /* Hurrah */
    return (TRUE);
}
