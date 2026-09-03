#ifdef __cplusplus
extern "C" {
#endif

#include "controls.h"

typedef struct CVec2Char {
    char x, y;
} CVec2Char;

typedef struct Controls {
    SceCtrlData input;
    CVec2Char stick;
    unsigned int pressed;
    unsigned int held;
    unsigned int released;
} Controls;

static Controls pad;

static void controls_AddInput(const unsigned int button) {
    if ((pad.input.Buttons & button) != 0) {
		pad.released &= ~button;
		
		if ((pad.held & button) == 0) {
			pad.pressed |= button;
			pad.held |= button;
		}
	} else {
        if (pad.held & button)
			pad.released |= button;
		
		pad.pressed &= ~button;
		pad.held &= ~button;
	}
}

void controls_init(void) {
    memset(&pad, 0, sizeof(Controls));

    sceCtrlSetSamplingCycle(0);

    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    sceCtrlSetIdleCancelThreshold(25, 25);
}

void controls_read(void) {
    sceCtrlPeekBufferPositive(&pad.input, 1);

    pad.pressed = 0;
    pad.released = 0;

    pad.stick.x = (signed) pad.input.Lx - 128;
    pad.stick.y = (signed) pad.input.Ly - 128;

    controls_AddInput(PSP_CTRL_SELECT);
    controls_AddInput(PSP_CTRL_START);
    controls_AddInput(PSP_CTRL_UP);
    controls_AddInput(PSP_CTRL_RIGHT);
    controls_AddInput(PSP_CTRL_DOWN);
    controls_AddInput(PSP_CTRL_LEFT);
    controls_AddInput(PSP_CTRL_LTRIGGER);
    controls_AddInput(PSP_CTRL_RTRIGGER);
    controls_AddInput(PSP_CTRL_TRIANGLE);
    controls_AddInput(PSP_CTRL_CIRCLE);
    controls_AddInput(PSP_CTRL_CROSS);
    controls_AddInput(PSP_CTRL_SQUARE);
    controls_AddInput(PSP_CTRL_HOME);
}

bool controls_pressed(const unsigned int button) {
    return ((pad.pressed & button) == button);
}

bool controls_held(const unsigned int button) {
    return ((pad.held & button) == button);
}

bool controls_released(const unsigned int button) {
    return ((pad.released & button) == button);
}

int controls_AnalogX(void) {
    return pad.stick.x;
}

int controls_AnalogY(void) {
    return pad.stick.y;
}

#ifdef __cplusplus
}
#endif