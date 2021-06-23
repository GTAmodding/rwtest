
typedef struct ControllerState ControllerState;
struct ControllerState
{
	int16 LeftStickX, LeftStickY;
	int16 RightStickX, RightStickY;
	int16 LeftShoulder1, LeftShoulder2;
	int16 RightShoulder1, RightShoulder2;
	int16 DPadUp, DPadDown, DPadLeft, DPadRight;
	int16 Start, Select;
	int16 Square, Triangle, Cross, Circle;
	int16 LeftShock, RightShock; 
	int16 NetworkTalk;

//	float GetLeftStickX(void) { return LeftStickX/32767.0f; };
//	float GetLeftStickY(void) { return LeftStickY/32767.0f; };
//	float GetRightStickX(void) { return RightStickX/32767.0f; };
//	float GetRightStickY(void) { return RightStickY/32767.0f; };
};

typedef struct Pad Pad;
struct Pad
{
	ControllerState OldState, NewState;
	int Phase;
};
extern Pad pad1;

RwBool SkyPadDetect(void);
RwBool PadInit(void);
void UpdatePad(Pad *pad, int port);
