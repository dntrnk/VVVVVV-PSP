#ifndef VVV_COMPAT_H
#define VVV_COMPAT_H

// Replacement for SDL types and functions with our own implementations
// so we don't depend on SDL.

#include <cstdint>

typedef struct {
    int x, y;
    int w, h;
} VVV_Rect;

typedef struct {
    int x;
    int y;
} VVV_Point;

char *VVV_GetBasePath(void);

void VVV_TicksInit(void);
uint32_t VVV_GetTicks(void);
void VVV_Delay(uint32_t ms);

bool VVV_PointInRect(const VVV_Point *p, const VVV_Rect *r);

void VVV_ShowSimpleMessageBox(const char* title, const char* message);

#endif // VVV_COMPAT_H