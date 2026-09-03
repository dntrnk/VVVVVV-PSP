#include "glib2d_helper.h"

#ifdef __cplusplus
extern "C" {
#endif

static struct {
    g2dImage* current_tex;
    bool is_batching;
} _batch = {
    .current_tex = NULL,
    .is_batching = false
};

static void _flush_batch(void) {
    if (_batch.is_batching) {
        g2dEnd();
        _batch.current_tex = NULL;
        _batch.is_batching = false;
    }
}

static void _check_batch(g2dImage* tex) {
    if (_batch.is_batching && _batch.current_tex != tex) {
        _flush_batch();
    }

    if (!_batch.is_batching) {
        g2dBeginRects(tex);
        _batch.current_tex = tex;
        _batch.is_batching = true;
    }
}

void g2dHelperClear(g2dColor color) {
    _flush_batch();
    g2dClear(color);
}

void g2dHelperFlip(void) {
    _flush_batch();
    g2dFlip(G2D_VSYNC);
}

void g2dHelperFillRect(int x, int y, int w, int h, g2dColor color) {
    _check_batch(NULL);
    g2dReset();
    g2dSetColor(color);
    g2dSetCoordXY(x, y);
    g2dSetScaleWH(w, h);
    g2dAdd();
}

void g2dHelperDrawRect(int x, int y, int w, int h, g2dColor color) {
    _check_batch(NULL);
    g2dReset();
    g2dSetColor(color);
    g2dSetCoordXY(x, y); g2dAdd();
    g2dSetCoordXY(x + w - 1, y); g2dAdd();
    g2dSetCoordXY(x + w - 1, y + h - 1); g2dAdd();
    g2dSetCoordXY(x, y + h - 1); g2dAdd();
    g2dSetCoordXY(x, y); g2dAdd();
    g2dAdd();
}

void g2dHelperDrawImage(g2dImage* tex, int x, int y, int w, int h, g2dColor color, int srcx, int srcy, int srcw, int srch) {
    _check_batch(tex);
    g2dReset();
    g2dSetColor(color);
    g2dSetCoordXY(x, y);
    g2dSetCropXY(srcx, srcy);
    g2dSetCropWH(srcw, srch);
    g2dSetScaleWH(w, h);
    g2dAdd();
}

#ifdef __cplusplus
}
#endif