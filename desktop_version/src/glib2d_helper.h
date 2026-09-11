#ifndef G2D_HELPER_H
#define G2D_HELPER_H

#ifdef __cplusplus
extern "C" {
#endif

#include "glib2d.h"

void g2dHelperClear(g2dColor color);
void g2dHelperFlip(void);
void g2dHelperFillRect(int x, int y, int w, int h, g2dColor color);
void g2dHelperDrawRect(int x, int y, int w, int h, g2dColor color);
void g2dHelperDrawImage(g2dImage* tex, int x, int y, int w, int h, g2dColor color, int srcx, int srcy, int srcw, int srch);

#ifdef __cplusplus
}
#endif

#endif // G2D_HELPER_H