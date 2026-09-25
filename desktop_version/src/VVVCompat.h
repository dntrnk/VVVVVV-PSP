#ifndef VVV_COMPAT_H
#define VVV_COMPAT_H

// Replacement for SDL types and functions with our own implementations
// so we don't depend on SDL.

typedef struct {
    int x, y;
    int w, h;
} VVV_Rect;

typedef struct {
    int x;
    int y;
} VVV_Point;

bool VVV_PointInRect(const VVV_Point *p, const VVV_Rect *r);

#endif // VVV_COMPAT_H