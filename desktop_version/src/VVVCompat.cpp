#include "VVVCompat.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>

#include <pspkernel.h>
#include <pspdebug.h>
#include <pspdisplay.h>
#include <pspctrl.h>
#include <psprtc.h>
#include <unistd.h>

static uint64_t start_tick;

char *VVV_GetBasePath(void)
{
    char *retval = NULL;
    size_t len;
    char cwd[PATH_MAX];

    getcwd(cwd, sizeof(cwd));
    len = strlen(cwd) + 2;
    retval = (char *)malloc(len);
    snprintf(retval, len, "%s/", cwd);

    return retval;
}

static uint64_t PSP_Ticks(void)
{
    uint64_t ticks;
    sceRtcGetCurrentTick(&ticks);
    return ticks;
}

void VVV_TicksInit(void)
{
    if (start_tick == 0) {
        start_tick = PSP_Ticks();
    }
}

uint32_t VVV_GetTicks(void)
{
    return (uint32_t)(((PSP_Ticks() - start_tick) / 1000ULL) & 0xFFFFFFFF);
}

void VVV_Delay(uint32_t ms)
{
    const uint32_t max_delay = 0xffffffffUL / 1000;
    if (ms > max_delay) {
        ms = max_delay;
    }
    sceKernelDelayThreadCB(ms * 1000);
}

bool VVV_PointInRect(const VVV_Point *p, const VVV_Rect *r)
{
    return ( (p->x >= r->x) && (p->x < (r->x + r->w)) &&
             (p->y >= r->y) && (p->y < (r->y + r->h)) ) ? true : false;
}

void VVV_ShowSimpleMessageBox(const char* title, const char* message)
{
    pspDebugScreenInit();
    pspDebugScreenClear();
    pspDebugScreenSetXY(0, 0);
    pspDebugScreenPrintf("=== %s ===\n\n%s\n\nPress any button to exit...\n", title, message);

    sceCtrlSetSamplingCycle(0);
    sceCtrlSetSamplingMode(PSP_CTRL_MODE_ANALOG);

    SceCtrlData pad;
    unsigned int prev = 0;

    for (;;)
    {
        sceCtrlReadBufferPositive(&pad, 1);

        unsigned int pressed = pad.Buttons & ~prev;
        prev = pad.Buttons;

        if (pressed != 0)
        {
            break;
        }

        sceDisplayWaitVblankStart();
    }
}