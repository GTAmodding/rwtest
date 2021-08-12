#include <stdio.h>
#include <string.h>
#include <kernel.h>
#include <sif.h>
#include <sifrpc.h>
#include <introld.h>
#include <libcdvd.h>

#define MYNAME "cdvd_stream"
ModuleInfo Module = { MYNAME, 0x0101 };
extern libhead mylib_entry;

typedef struct Queue Queue;
struct Queue
{
	int *items;
	int head;
	int tail;
	int size;
};

typedef struct ReadInfo ReadInfo;
struct ReadInfo
{
	int pos;
	int len;
	u_char *buf;
	u_char toEE;
	u_char signalWhenDone;
	u_char busy;
	int status;
	int sema;
};

// EE knows about these too, so put them in a header
enum
{
	CDSTATUS_NOERR	= 0,
	CDSTATUS_SUCCESS	= 1,	// not SCECdErABRT?
	CDSTATUS_READING	= 0xFF,
	CDSTATUS_ERROR	= 0xFE,	// SCECdErREADCFR
	CDSTATUS_ERROR_NOCD	= 0xFD,	// SCECdErREADCF
	CDSTATUS_ERROR_WRONGCD	= 0xFC,
	CDSTATUS_ERROR_OPENCD	= 0xFB,
	CDSTATUS_WAITING	= 0xFA,
};

int gCdStreamInitialised;
u_char *pCdStreamToEEBuffer;
struct {
	int lastPosn;
	int status;
	int unused;
	int diskType;
	int trayStatus;
} gDiskStatus;
Queue gChannelRequestQ;
int gNumChannels;
ReadInfo *gpReadInfo;
sceCdRMode gCdReadMode;
int gCdStreamSema;
unsigned int gRpcOutArgsBuffer[64];
unsigned int gRpcInArgsBuffer[64];

// EE transfers are done in units of 8 sectors
// in a double buffer fashion. One buffer is
// read into while the other is DMAed to the EE
#define NUMSECTORS 8
#define BUFSIZE(n) ((n)<<11)	// *2048

int CdStreamThread(void);

void
CdStreamInit(int n)
{
	int i;
	struct SemaParam sp;
	struct ThreadParam tp;
	int th;

	if(gCdStreamInitialised)
		return;

	gNumChannels = n;
	gCdStreamInitialised = 1;
	CpuDisableIntr();
	pCdStreamToEEBuffer = AllocSysMemory(0, 2*BUFSIZE(NUMSECTORS), 0);
	gChannelRequestQ.items = AllocSysMemory(0, sizeof(int)*(gNumChannels+1), 0);
	gpReadInfo = AllocSysMemory(0, sizeof(ReadInfo)*gNumChannels, 0);
	CpuEnableIntr();
	printf("%s: buffer %p\n", MYNAME, pCdStreamToEEBuffer);
	printf("%s: request queue %p\n", MYNAME, gChannelRequestQ.items);
	printf("%s: read info %p\n", MYNAME, gpReadInfo);

	gCdReadMode.trycount = 32;
	gCdReadMode.spindlctrl = SCECdSpinNom;
	gCdReadMode.datapattern = SCECdSecS2048;

	for(i = 0; i < gNumChannels; i++){
		gpReadInfo[i].status = CDSTATUS_NOERR;
		gpReadInfo[i].len = 0;
		gpReadInfo[i].toEE = 0;
		gpReadInfo[i].signalWhenDone = 0;
		gpReadInfo[i].busy = 0;

		sp.attr = SA_THFIFO;
		sp.initCount = 0;
		sp.maxCount = 2;
		sp.option = i + 2*BUFSIZE(NUMSECTORS);	// unused
		gpReadInfo[i].sema = CreateSema(&sp);
		if(gpReadInfo[i].sema < 0){
			printf("%s: failed to create sync semaphore\n", MYNAME);
			return;
		}
	}

	gChannelRequestQ.head = 0;
	gChannelRequestQ.tail = 0;
	gChannelRequestQ.size = gNumChannels + 1;

	gDiskStatus.lastPosn = 0;
	gDiskStatus.status = CDSTATUS_NOERR;
	gDiskStatus.diskType = sceCdGetDiskType();
	sceCdTrayReq(SCECdTrayCheck, &gDiskStatus.trayStatus);
	if(gDiskStatus.diskType == SCECdPS2DVD){
		sp.attr = SA_THFIFO;
		sp.initCount = 0;
		sp.maxCount = n;
		sp.option = 0;
		gCdStreamSema = CreateSema(&sp);
		if(gCdStreamSema < 0){
			printf("%s: failed to create stream semaphore\n", MYNAME);
			return;
		}
		tp.attr = TH_C;
		tp.entry = CdStreamThread;
		tp.initPriority = 82;
		tp.stackSize = 2048;
		tp.option = 0;
		th = CreateThread(&tp);
		if(th > 0){
			StartThread(th, 0);
			printf("%s: Start Streaming Thread\n", MYNAME);
		}else
			printf("%s: failed to create streaming thread\n", MYNAME);
	}
}

void
AddToQueue(Queue *q, int item)
{
	CpuDisableIntr();
	q->items[q->tail++] = item;
	q->tail %= q->size;
	if(q->head == q->tail)
		Kprintf("Queue is full\n");
	CpuEnableIntr();
}

int
GetFirstInQueue(Queue *q)
{
	int ret;
	CpuDisableIntr();
	if(q->head == q->tail)
		ret = -1;
	else
		ret = q->items[q->head];
	CpuEnableIntr();
	return ret;
}

void
RemoveFirstInQueue(Queue *q)
{
	CpuDisableIntr();
	if(q->head == q->tail)
		Kprintf("Queue is empty\n");
	else{
		q->head++;
		q->head %= q->size;
	}
	// BUGFIX: this was in the else branch!
	CpuEnableIntr();
}

int
GetLastInQueue(Queue *q)
{
	int ret;
	CpuDisableIntr();
	if(q->head == q->tail)
		ret = -1;
	else
		ret = q->items[q->tail == 0 ? q->size-1 : q->tail-1];
	CpuEnableIntr();
	return ret;
}

int
CdStreamRead(int ch, u_char *buf, int pos, int len)
{
	ReadInfo *ri;
	ri = &gpReadInfo[ch];
	if(ri->len || ri->busy)
		return 0;

	ri->status = CDSTATUS_NOERR;
	ri->pos = pos;
	ri->len = len;
	ri->buf = buf;
	ri->toEE = 0;
	ri->signalWhenDone = 0;
	AddToQueue(&gChannelRequestQ, ch);
	if(SignalSema(gCdStreamSema))
		printf("Signal Sema Error\n");
	return 1;
}

int
CdStreamReadToEE(int ch, u_char *buf, int pos, int len)
{
	ReadInfo *ri;
	ri = &gpReadInfo[ch];
	if(ri->len || ri->busy){
		printf("%s: read request failed\n", MYNAME);
		return 0;
	}else if(len == 0){
		ri->status = CDSTATUS_ERROR;
		return 0;
	}
	ri->status = CDSTATUS_NOERR;
	ri->pos = pos;
	ri->len = len;
	ri->buf = buf;
	ri->toEE = 1;
	ri->signalWhenDone = 0;
	AddToQueue(&gChannelRequestQ, ch);
	if(SignalSema(gCdStreamSema))
		printf("Signal Sema Error\n");
	return 1;
}

int
CdStreamGetStatus(int ch)
{
	int ret;
	ReadInfo *ri;
	ri = &gpReadInfo[ch];
	if(ri->busy);
		return CDSTATUS_READING;
	if(ri->len)
		return CDSTATUS_WAITING;
	if(ri->status == CDSTATUS_NOERR)
		return CDSTATUS_NOERR;
	ret = ri->status;
	ri->status = CDSTATUS_NOERR;
	return ret;
}

int
CdStreamGetLastPosn(void)
{
	int ch;
	ch = GetLastInQueue(&gChannelRequestQ);
	if(ch == -1)
		return gDiskStatus.lastPosn;
	else
		return gpReadInfo[ch].pos + gpReadInfo[ch].len;
}

int
CdStreamSync(int ch)
{
	ReadInfo *ri;
	ri = &gpReadInfo[ch];
	CpuDisableIntr();
	if(ri->len){
		ri->signalWhenDone = 1;
		CpuEnableIntr();
		WaitSema(ri->sema);
		ri->busy = 0;
	}else{
		ri->busy = 0;
		CpuEnableIntr();
	}
	return ri->status;
}

int
CdStreamGetCdCheck(void)
{
	FlushDcache();
	sceCdDiskReady(0);
	if(!sceCdRead(800, 1, pCdStreamToEEBuffer, &gCdReadMode))
		return CDSTATUS_ERROR;
	sceCdSync(0);
	if(sceCdGetError())
		return CDSTATUS_ERROR;
	if(strncmp("BOOT2 = cdrom0:\\", pCdStreamToEEBuffer, 16) == 0)
		return CDSTATUS_NOERR;
	return CDSTATUS_ERROR_WRONGCD;
}

int
CdStreamThread(void)
{
	int ch;
	ReadInfo *ri;
	int trayStatus;

	sceCdDiskReady(0);
	for(;;){
		do WaitSema(gCdStreamSema);
		while((ch = GetFirstInQueue(&gChannelRequestQ)) == -1);

		ri = &gpReadInfo[ch];
		ri->busy = 1;
		sceCdTrayReq(SCECdTrayCheck, &trayStatus);

		// Handle tray changes and errors
		if(trayStatus != gDiskStatus.trayStatus || gDiskStatus.status != CDSTATUS_NOERR){
			if(sceCdStatus() == SCECdStatShellOpen)
				ri->status = CDSTATUS_ERROR_OPENCD;
			else if(sceCdDiskReady(1) == SCECdNotReady)
				ri->status = gDiskStatus.status;
			else{
				int type = sceCdGetDiskType();
				if(type == SCECdDETCT && gDiskStatus.status != CDSTATUS_NOERR)
					ri->status = gDiskStatus.status;
				else if(type == SCECdNODISC)
					ri->status = CDSTATUS_ERROR_NOCD;
				else if(type == SCECdPS2DVD){
					ri->status = CdStreamGetCdCheck();
					if(ri->status == CDSTATUS_NOERR)
						sceCdDiskReady(0);
				}else
					ri->status = CDSTATUS_ERROR_WRONGCD;
			}
			gDiskStatus.trayStatus = trayStatus;
		}

		// do the transfer
		if(ri->status == CDSTATUS_NOERR){
			if(ri->toEE){
				sceSifDmaData dmaData[2];
				int curpos, curlen;
				int buf, dmaId, len;

				dmaId = -1;
				len = NUMSECTORS;
				// our two buffers
				dmaData[0].data = (u_int)pCdStreamToEEBuffer;
				dmaData[0].size = BUFSIZE(NUMSECTORS);
				dmaData[0].mode = 0;
				dmaData[0].addr = (u_int)ri->buf;
				dmaData[1].data = (u_int)(pCdStreamToEEBuffer + BUFSIZE(NUMSECTORS));
				dmaData[1].size = BUFSIZE(NUMSECTORS);
				dmaData[1].mode = 0;
				dmaData[1].addr = (u_int)(ri->buf + BUFSIZE(NUMSECTORS));
				curpos = ri->pos;
				curlen = ri->len;
				buf = 0;
				FlushDcache();

				while(curlen > 0){
					if(curlen < NUMSECTORS){
						len = curlen;
						dmaData[buf].size = BUFSIZE(curlen);
					}
					// this looks cleaner
					if(!sceCdRead(curpos, len, (void*)dmaData[buf].data, &gCdReadMode)){
//					if(!sceCdRead(curpos, len, pCdStreamToEEBuffer + buf*BUFSIZE(NUMSECTORS), &gCdReadMode)){
						ri->status = CDSTATUS_ERROR;
						break;
					}
					curpos += NUMSECTORS;
					sceCdSync(0);
					ri->status = sceCdGetError();
					if(ri->status != CDSTATUS_NOERR){
						printf("sceCdSync: Error %d\n", ri->status);
						break;
					}
					// wait for other transfer to finish
					if(dmaId != -1){
						while(sceSifDmaStat(dmaId) >= 0);
						dmaData[1-buf].addr += 2*BUFSIZE(NUMSECTORS);
					}
					// queue the new one
					do{
						CpuDisableIntr();
						dmaId = sceSifSetDma(&dmaData[buf], 1);
						CpuEnableIntr();
					}while(dmaId == 0);
					curlen -= NUMSECTORS;
					buf = 1-buf;
				}
				// wait again
				if(dmaId != 1)
					while(sceSifDmaStat(dmaId) >= 0);
			}else if(sceCdRead(ri->pos, ri->len, ri->buf, &gCdReadMode)){
				sceCdSync(0);
				ri->status = sceCdGetError();
			}else
				ri->status = CDSTATUS_ERROR;
		}

		gDiskStatus.lastPosn = ri->pos + ri->len;
		gDiskStatus.status = ri->status;
		RemoveFirstInQueue(&gChannelRequestQ);
		ri->len = 0;
		if(ri->signalWhenDone)
			SignalSema(ri->sema);
		ri->busy = 0;
	}
}

void*
CdStreamServiceFunc(unsigned int fno, void *data, int size)
{
	int *args = data;
	switch(fno){
	case 0:
		CdStreamInit(args[0]);
		break;
	case 1:
		gRpcOutArgsBuffer[0] = CdStreamReadToEE(args[0], (u_char*)args[1], args[2], args[3]);
		break;
	case 2:
		gRpcOutArgsBuffer[0] = CdStreamGetStatus(args[0]);
		break;
	case 3:
		gRpcOutArgsBuffer[0] = CdStreamSync(args[0]);
		break;
	case 4:
		gRpcOutArgsBuffer[0] = CdStreamGetLastPosn();
		break;
	}
	return gRpcOutArgsBuffer;
}

int
CdStreamServerThread(void)
{
	sceSifQueueData q;
	sceSifServeData s;

	sceSifSetRpcQueue(&q, GetThreadId());
	sceSifRegisterRpc(&s, 0x65686577, CdStreamServiceFunc, gRpcInArgsBuffer, 0, 0, &q);
	sceSifRpcLoop(&q);
	return 0;
}

int start()
{
	struct ThreadParam param;
	int th;

	FlushDcache();
	printf("%s: Loaded\n", MYNAME);
	CpuEnableIntr();
	if(!sceSifCheckInit())
		sceSifInit();
	sceSifInitRpc(0);
	if(RegisterLibraryEntries(&mylib_entry))
		return 1;
	printf("%s: Registered LibraryEntries\n", MYNAME);
	param.attr = TH_C;
	param.entry = CdStreamServerThread;
	param.initPriority = 47;
	param.stackSize = 2048;
	param.option = 0;
	th = CreateThread(&param);
	if(th > 0){
		StartThread(th, 0);
		printf("%s: Start RpcServer Thread\n", MYNAME);
		return 0;
	}else
		return 1;
}
