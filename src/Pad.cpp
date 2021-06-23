#include "common.h"
#include "rwcore.h"

#include "eeregs.h"
#include "sifdev.h"
#include "sifrpc.h"

#include "libpad.h"

#include "Pad.h"

static unsigned char ReadData[32] __attribute__ ((aligned(64)));
static u_long128    PadDmaBuffer[scePadDmaBufferMax] __attribute__ ((aligned(64)));
static u_long128    PadDmaBuffer2[scePadDmaBufferMax] __attribute__ ((aligned(64)));

Pad pad1;


RwBool
PadInit(void)
{
	scePadInit(0);
	scePadPortOpen(0, 0, PadDmaBuffer);
	scePadPortOpen(1, 0, PadDmaBuffer2);

	return TRUE;
}

#define Min(x, y) ((x) < (y) ? (x) : (y))
#define Max(x, y) ((x) > (y) ? (x) : (y))

void
UpdatePad(Pad *pad, int port)
{
	unsigned int PadData;
	int id, ext_id;
	int state;

	pad->OldState = pad->NewState;
	memset(&pad->NewState, 0, sizeof(pad->NewState));

	state = scePadGetState(port, 0);

	switch(pad->Phase){
	// Detect
	case 0:
		// wait for stable state
		if(state != scePadStateStable && state != scePadStateFindCTP1)
			break;

		id = scePadInfoMode(port, 0, InfoModeCurID, 0);
		if(id==0) break;

		ext_id = scePadInfoMode(port, 0, InfoModeCurExID,0);
		if(ext_id>0) id = ext_id;

		switch(id) {
		/* STANDARD */
		case 4:
			pad->Phase = 40;
			break;

		/* ANALOG */
		case 7:
			pad->Phase = 70;
			break;

		default:
			pad->Phase = 99;
			break;
		}
		break;

	// Standard Controller
	case 40:
		// check if there are modes
		if (scePadInfoMode(port, 0, InfoModeIdTable, -1)==0) {
			pad->Phase = 99;
			break;
		}
		pad->Phase++;
		// fall through
	case 41:
		// set mode to analog but don't lock
		if (scePadSetMainMode(port, 0, 1, 0)==1)
			pad->Phase++;
		break;
	case 42:
		// wait until set
		if (scePadGetReqState(port, 0)==scePadReqStateFaild)
			pad->Phase--;
		// check info again, should now be analog
		if (scePadGetReqState(port, 0)==scePadReqStateComplete)
			pad->Phase = 0;
		break;

	// DualShock2
	case 70:
		// check if we can have pressure sensitive buttons
		if (scePadInfoPressMode(port,0)==1) {
			pad->Phase = 76;
			break;
		}
		pad->Phase = 99;
		break;
	case 76:
		// yes, enable
		if (scePadEnterPressMode(port, 0)==1)
			pad->Phase++;
		break;
	case 77:
		// wait until enabled
		if (scePadGetReqState(port, 0)==scePadReqStateFaild)
			pad->Phase--;
		if (scePadGetReqState(port, 0)==scePadReqStateComplete)
			pad->Phase = 99;
		break;

	default:
		// wait for stable state
		if(state != scePadStateStable && state != scePadStateFindCTP1)
			break;

		if(scePadRead(port, 0, ReadData) > 0)
			PadData = 0xffff ^ ((ReadData[2] << 8) | ReadData[3]);
		else
			PadData = 0;

		if(PadData & SCE_PADLup) pad->NewState.DPadUp = 255;
		if(PadData & SCE_PADLdown) pad->NewState.DPadDown = 255;
		if(PadData & SCE_PADLleft) pad->NewState.DPadLeft = 255;
		if(PadData & SCE_PADLright) pad->NewState.DPadRight = 255;
		if(PadData & SCE_PADstart) pad->NewState.Start = 255;
		if(PadData & SCE_PADselect) pad->NewState.Select = 255;
		if(PadData & SCE_PADRup) pad->NewState.Triangle = 255;
		if(PadData & SCE_PADRdown) pad->NewState.Cross = 255;
		if(PadData & SCE_PADRleft) pad->NewState.Square = 255;
		if(PadData & SCE_PADRright) pad->NewState.Circle = 255;
		if(PadData & SCE_PADL1) pad->NewState.LeftShoulder1 = 255;
		if(PadData & SCE_PADL2) pad->NewState.LeftShoulder2 = 255;
		if(PadData & SCE_PADR1) pad->NewState.RightShoulder1 = 255;
		if(PadData & SCE_PADR2) pad->NewState.RightShoulder2 = 255;
		if(PadData & SCE_PADi) pad->NewState.LeftShock = 255;
		if(PadData & SCE_PADj) pad->NewState.RightShock = 255;
#define CLAMP_AXIS(x) (((x) < 43 && (x) >= -42) ? 0 : (((x) > 0) ? (Max((x)-42, 0)*127/85) : Min((x)+42, 0)*127/85))
#define GET_AXIS(x) CLAMP_AXIS((x)-128)
		pad->NewState.RightStickX = GET_AXIS((int16)ReadData[4]);
		pad->NewState.RightStickY = GET_AXIS((int16)ReadData[5]);
		pad->NewState.LeftStickX = GET_AXIS((int16)ReadData[6]);
		pad->NewState.LeftStickY = GET_AXIS((int16)ReadData[7]);
	}
}

