// gLib2D by Geecko - A simple, fast, light-weight 2D graphics library.
//
// This work is licensed under the Creative Commons BY-SA 3.0 Unported License.
// See LICENSE for more details.
//
// Please report bugs at : geecko.dev@free.fr

#ifdef __cplusplus
extern "C" {
#endif

#include "glib2d.h"

#include <stdlib.h>
#include <pspkernel.h>
#include <pspdisplay.h>
#include <pspgu.h>
#include <vram.h>
#include <malloc.h>
#include <math.h>

// В начале файла, после include'ов
#ifndef GU_PSM_8888
#define GU_PSM_8888 3
#endif

#ifndef GU_PSM_T4
#define GU_PSM_T4 4
#endif

#ifndef GU_PSM_T8
#define GU_PSM_T8 5
#endif
//

#define PSP_LINE_SIZE       (512)
#define PIXEL_SIZE          (4)
#define FRAMEBUFFER_SIZE    (PSP_LINE_SIZE*G2D_SCR_H*PIXEL_SIZE)
#define MALLOC_STEP         (128)
#define TRANSFORM_STACK_MAX (64)
#define SLICE_WIDTH         (64.f)
#define M_180_PI            (57.29578f)
#define M_PI_180            (0.017453292f)

#define DEFAULT_SIZE       (10)
#define DEFAULT_COORD_MODE (G2D_UP_LEFT)
#define DEFAULT_X          (0.f)
#define DEFAULT_Y          (0.f)
#define DEFAULT_Z          (0.f)
#define DEFAULT_COLOR      (G2D_WHITE)
#define DEFAULT_ALPHA      (0xFF)

#define CURRENT_OBJ obj_list[obj_list_size-1]
#define CURRENT_TRANSFORM transform_stack[transform_stack_size-1]
#define I_OBJ obj_list[i]

typedef enum
{
    RECTS, LINES, QUADS, POINTS
} Obj_Type;

typedef struct
{
    float x, y, z;
    float rot, rot_sin, rot_cos;
    float scale_w, scale_h;
} Transform;

typedef struct
{
    float x, y, z;
    float rot_x, rot_y; // Rotation center
    float rot, rot_sin, rot_cos;
    int crop_x, crop_y;
    int crop_w, crop_h;
    float scale_w, scale_h;
    g2dColor color;
    g2dAlpha alpha;
} Object;


// * Main vars *
static int *list;
static bool init = false, start = false, zclear = true, scissor = false;
static Transform transform_stack[TRANSFORM_STACK_MAX];
static int transform_stack_size;
static float global_scale = 1.f;
// * Object vars *
static Object *obj_list = NULL, obj;
static Obj_Type obj_type;
static int obj_list_size;
static bool obj_begin = false, obj_line_strip;
static bool obj_use_z, obj_use_vert_color, obj_use_blend, obj_use_rot,
obj_use_tex_linear, obj_use_tex_repeat, obj_use_int;
static g2dCoord_Mode obj_coord_mode;
static int obj_colors_count;
static g2dImage *obj_tex;
static int obj_camera_x, obj_camera_y;
static bool obj_use_camera = false;

float g2dScreenOffsetX = 80.0f;
float g2dScreenOffsetY = 16.0f;

g2dImage g2d_draw_buffer = { 512, 512, G2D_SCR_W, G2D_SCR_H,
                             (float)G2D_SCR_W / G2D_SCR_H, false, false,
                             (g2dColor *)FRAMEBUFFER_SIZE },
    g2d_disp_buffer = { 512, 512, G2D_SCR_W, G2D_SCR_H,
                        (float)G2D_SCR_W / G2D_SCR_H, false, false,
                        (g2dColor *)0 };

// * Internal functions *

void _g2dStart() {
    if (!init) g2dInit();

    sceKernelDcacheWritebackAll();
    sceGuStart(GU_DIRECT, list);
    start = true;
}


// Vertex order: [texture uv] [color] [vertex]
void *_g2dSetVertex(void *vp, int i, float vx, float vy) {
    short *v_p_short = vp;
    g2dColor *v_p_color;
    float *v_p_float;

    // Texture
    if (obj_tex != NULL) {
        *(v_p_short++) = I_OBJ.crop_x + vx * I_OBJ.crop_w;
        *(v_p_short++) = I_OBJ.crop_y + vy * I_OBJ.crop_h;
    }

    // Color
    v_p_color = (g2dColor *)v_p_short;

    if (obj_use_vert_color) {
        *(v_p_color++) = I_OBJ.color;
    }

    // Coord
    v_p_float = (float *)v_p_color;

    v_p_float[0] = I_OBJ.x + (obj_type == RECTS ? vx * I_OBJ.scale_w : 0.f) + g2dScreenOffsetX;
    v_p_float[1] = I_OBJ.y + (obj_type == RECTS ? vy * I_OBJ.scale_h : 0.f) + g2dScreenOffsetY;

    // Then apply the rotation
    if (obj_use_rot && obj_type == RECTS) {
        float tx = v_p_float[0] - I_OBJ.rot_x, ty = v_p_float[1] - I_OBJ.rot_y;
        v_p_float[0] = I_OBJ.rot_x - I_OBJ.rot_sin * ty + I_OBJ.rot_cos * tx,
            v_p_float[1] = I_OBJ.rot_y + I_OBJ.rot_cos * ty + I_OBJ.rot_sin * tx;
    }

    if (obj_use_int) {
        v_p_float[0] = floorf(v_p_float[0]);
        v_p_float[1] = floorf(v_p_float[1]);
    }
    v_p_float[2] = I_OBJ.z;

    v_p_float += 3;

    return (void *)v_p_float;
}

// * Main functions *

void g2dInit() {
    if (init) return;

    // Display list allocation
    list = malloc(512 * 1024);

    // Init & setup GU
    sceGuInit();
    sceGuStart(GU_DIRECT, list);

    sceGuDrawBuffer(GU_PSM_8888, g2d_draw_buffer.data, PSP_LINE_SIZE);
    sceGuDispBuffer(G2D_SCR_W, G2D_SCR_H, g2d_disp_buffer.data, PSP_LINE_SIZE);
    sceGuDepthBuffer((void *)(FRAMEBUFFER_SIZE * 2), PSP_LINE_SIZE);
    sceGuOffset(2048 - (G2D_SCR_W / 2), 2048 - (G2D_SCR_H / 2));
    sceGuViewport(2048, 2048, G2D_SCR_W, G2D_SCR_H);

    g2d_draw_buffer.data = vabsptr(g2d_draw_buffer.data);
    g2d_disp_buffer.data = vabsptr(g2d_disp_buffer.data);

    g2dResetScissor();
    sceGuDepthRange(65535, 0);
    sceGuClearDepth(65535);
    sceGuAlphaFunc(GU_GREATER, 0, 0xff);
    sceGuDepthFunc(GU_LEQUAL);
    sceGuBlendFunc(GU_ADD, GU_SRC_ALPHA, GU_ONE_MINUS_SRC_ALPHA, 0, 0);
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    sceGuShadeModel(GU_SMOOTH);

    sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_CLIP_PLANES);
    sceGuDisable(GU_DITHER);
    sceGuEnable(GU_ALPHA_TEST);
    sceGuEnable(GU_SCISSOR_TEST);
    sceGuEnable(GU_BLEND);

    sceGuFinish();
    sceGuSync(0, 0);
    sceDisplayWaitVblankStart();
    sceGuDisplay(GU_TRUE);

    init = true;
}


void g2dTerm() {
    if (!init) return;

    sceGuTerm();

    free(list);

    init = false;
}

void g2dClear(g2dColor color) {
    if (!start) _g2dStart();

    sceGuClearColor(color);
    sceGuClear(GU_COLOR_BUFFER_BIT | GU_FAST_CLEAR_BIT
        | (zclear ? GU_DEPTH_BUFFER_BIT : 0));
    zclear = false;
}


void g2dClearZ() {
    if (!start) _g2dStart();

    sceGuClear(GU_DEPTH_BUFFER_BIT | GU_FAST_CLEAR_BIT);
    zclear = true;
}


void _g2dBeginCommon() {
    if (!start) _g2dStart();

    obj_list_size = 0;
    obj_list = realloc(obj_list, MALLOC_STEP * sizeof(Object));

    obj_use_z = false;
    obj_use_vert_color = false;
    obj_use_blend = false;
    obj_use_rot = false;
    obj_use_int = false;
    obj_colors_count = 0;
    g2dReset();

    obj_begin = true;
}


void g2dBeginRects(g2dImage *tex) {
    if (obj_begin) return;

    obj_type = RECTS;
    obj_tex = tex;
    _g2dBeginCommon();
}


void g2dBeginLines(g2dLine_Mode mode) {
    if (obj_begin) return;

    obj_type = LINES;
    obj_tex = NULL;
    obj_line_strip = (mode & G2D_STRIP);
    _g2dBeginCommon();
}


void g2dBeginQuads(g2dImage *tex) {
    if (obj_begin) return;

    obj_type = QUADS;
    obj_tex = tex;
    _g2dBeginCommon();
}


void g2dBeginPoints() {
    if (obj_begin) return;

    obj_type = POINTS;
    obj_tex = NULL;
    _g2dBeginCommon();
}


void _g2dEndRects() {
    // Define vertices properties
    int prim = (obj_use_rot ? GU_TRIANGLES : GU_SPRITES),
        v_obj_nbr = (obj_use_rot ? 6 : 2),
        v_nbr,
        v_coord_size = 3,
        v_tex_size = (obj_tex != NULL ? 2 : 0),
        v_color_size = (obj_use_vert_color ? 1 : 0),
        v_size = v_tex_size * sizeof(short) +
        v_color_size * sizeof(g2dColor) +
        v_coord_size * sizeof(float),
        v_type = GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        i;

    if (obj_tex != NULL)    v_type |= GU_TEXTURE_16BIT;
    if (obj_use_vert_color) v_type |= GU_COLOR_8888;

    // Count how many vertices to allocate.
    if (obj_tex == NULL || obj_use_rot) // No slicing
    {
        v_nbr = v_obj_nbr * obj_list_size;
    } else // Can use texture slicing for tremendous performance :)
    {
        for (v_nbr = 0, i = 0; i < obj_list_size; i++) {
            v_nbr += v_obj_nbr * ceilf(I_OBJ.crop_w / SLICE_WIDTH);
        }
    }

    // Allocate vertex list memory
    void *v = sceGuGetMemory(v_nbr * v_size), *vi = v;

    // Build the vertex list
    for (i = 0; i < obj_list_size; i += 1) {
        if (!obj_use_rot) {
            if (obj_tex == NULL) {
                vi = _g2dSetVertex(vi, i, 0.f, 0.f);
                vi = _g2dSetVertex(vi, i, 1.f, 1.f);
            } else // Use texture slicing
            {
                float u, step = SLICE_WIDTH / I_OBJ.crop_w;
                for (u = 0.f; u < 1.f; u += step) {
                    vi = _g2dSetVertex(vi, i, u, 0.f);
                    vi = _g2dSetVertex(vi, i, ((u + step) > 1.f ? 1.f : u + step), 1.f);
                }
            }
        } else // Rotation : draw 2 triangles per obj
        {
            vi = _g2dSetVertex(vi, i, 0.f, 0.f);
            vi = _g2dSetVertex(vi, i, 1.f, 0.f);
            vi = _g2dSetVertex(vi, i, 0.f, 1.f);
            vi = _g2dSetVertex(vi, i, 0.f, 1.f);
            vi = _g2dSetVertex(vi, i, 1.f, 0.f);
            vi = _g2dSetVertex(vi, i, 1.f, 1.f);
        }
    }

    // Then put it in the display list.
    sceGuDrawArray(prim, v_type, v_nbr, NULL, v);
}


void _g2dEndLines() {
    // Define vertices properties
    int prim = (obj_line_strip ? GU_LINE_STRIP : GU_LINES),
        v_obj_nbr = (obj_line_strip ? 1 : 2),
        v_nbr = v_obj_nbr * (obj_line_strip ? obj_list_size
            : obj_list_size / 2),
        v_coord_size = 3,
        v_color_size = (obj_use_vert_color ? 1 : 0),
        v_size = v_color_size * sizeof(g2dColor) +
        v_coord_size * sizeof(float),
        v_type = GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        i;

    if (obj_use_vert_color) v_type |= GU_COLOR_8888;

    // Allocate vertex list memory
    void *v = sceGuGetMemory(v_nbr * v_size), *vi = v;

    // Build the vertex list
    if (obj_line_strip) {
        vi = _g2dSetVertex(vi, 0, 0.f, 0.f);
        for (i = 1; i < obj_list_size; i += 1) {
            vi = _g2dSetVertex(vi, i, 0.f, 0.f);
        }
    } else {
        for (i = 0; i + 1 < obj_list_size; i += 2) {
            vi = _g2dSetVertex(vi, i, 0.f, 0.f);
            vi = _g2dSetVertex(vi, i + 1, 0.f, 0.f);
        }
    }

    // Then put it in the display list.
    sceGuDrawArray(prim, v_type, v_nbr, NULL, v);
}


void _g2dEndQuads() {
    // Define vertices properties
    int prim = GU_TRIANGLES,
        v_obj_nbr = 6,
        v_nbr = v_obj_nbr * (obj_list_size / 4),
        v_coord_size = 3,
        v_tex_size = (obj_tex != NULL ? 2 : 0),
        v_color_size = (obj_use_vert_color ? 1 : 0),
        v_size = v_tex_size * sizeof(short) +
        v_color_size * sizeof(g2dColor) +
        v_coord_size * sizeof(float),
        v_type = GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        i;

    if (obj_tex != NULL)    v_type |= GU_TEXTURE_16BIT;
    if (obj_use_vert_color) v_type |= GU_COLOR_8888;

    // Allocate vertex list memory
    void *v = sceGuGetMemory(v_nbr * v_size), *vi = v;

    // Build the vertex list
    for (i = 0; i + 3 < obj_list_size; i += 4) {
        vi = _g2dSetVertex(vi, i, 0.f, 0.f);
        vi = _g2dSetVertex(vi, i + 1, 1.f, 0.f);
        vi = _g2dSetVertex(vi, i + 3, 0.f, 1.f);
        vi = _g2dSetVertex(vi, i + 3, 0.f, 1.f);
        vi = _g2dSetVertex(vi, i + 1, 1.f, 0.f);
        vi = _g2dSetVertex(vi, i + 2, 1.f, 1.f);
    }

    // Then put it in the display list.
    sceGuDrawArray(prim, v_type, v_nbr, NULL, v);
}


void _g2dEndPoints() {
    // Define vertices properties
    int prim = GU_POINTS,
        v_obj_nbr = 1,
        v_nbr = v_obj_nbr * obj_list_size,
        v_coord_size = 3,
        v_color_size = (obj_use_vert_color ? 1 : 0),
        v_size = v_color_size * sizeof(g2dColor) +
        v_coord_size * sizeof(float),
        v_type = GU_VERTEX_32BITF | GU_TRANSFORM_2D,
        i;

    if (obj_use_vert_color) v_type |= GU_COLOR_8888;

    // Allocate vertex list memory
    void *v = sceGuGetMemory(v_nbr * v_size), *vi = v;

    // Build the vertex list
    for (i = 0; i < obj_list_size; i += 1) {
        vi = _g2dSetVertex(vi, i, 0.f, 0.f);
    }

    // Then put it in the display list.
    sceGuDrawArray(prim, v_type, v_nbr, NULL, v);
}


void g2dEnd() {
    if (!obj_begin || obj_list_size <= 0) {
        obj_begin = false;
        return;
    }

    if (obj_use_z)          sceGuEnable(GU_DEPTH_TEST);
    else                    sceGuDisable(GU_DEPTH_TEST);
    if (obj_use_blend)      sceGuEnable(GU_BLEND);
    else                    sceGuDisable(GU_BLEND);
    
    if (obj_use_vert_color) sceGuColor(G2D_WHITE);
    else                    sceGuColor(obj_list[0].color);

        if (obj_tex != NULL) {
        sceGuEnable(GU_TEXTURE_2D);

        if (obj_tex->format == 5 || obj_tex->format == 4) { // T8 или T4
            sceGuClutMode(GU_PSM_8888, 0, 0xFF, 0);
            int n_blocks = (obj_tex->format == 5) ? 32 : 2;
            sceGuClutLoad(n_blocks, obj_tex->palette);
        }

        sceGuTexMode(obj_tex->format, 0, 0, (obj_tex->swizzled ? 1 : 0));
        sceGuTexImage(0, obj_tex->tw, obj_tex->th, obj_tex->tw, obj_tex->data);
        
        // Синхронизация текстуры обязательна для CLUT форматов
        sceGuTexSync(); 
    } else {
        sceGuDisable(GU_TEXTURE_2D);
    }

    switch (obj_type) {
        case RECTS:  _g2dEndRects();  break;
        case LINES:  _g2dEndLines();  break;
        case QUADS:  _g2dEndQuads();  break;
        case POINTS: _g2dEndPoints(); break;
    }

    sceGuColor(G2D_WHITE);
    sceGuEnable(GU_BLEND);
    obj_begin = false;
    if (obj_use_z) zclear = true;
}


void g2dReset() {
    g2dResetCoord();
    g2dResetScale();
    g2dResetColor();
    g2dResetAlpha();
    g2dResetRotation();
    g2dResetCrop();
    g2dResetTex();
}


void g2dFlip(g2dFlip_Mode mode) {
    if (scissor) g2dResetScissor();

    sceGuFinish();
    sceGuSync(0, 0);
    if (mode & G2D_VSYNC) sceDisplayWaitVblankStart();

    g2d_disp_buffer.data = g2d_draw_buffer.data;
    g2d_draw_buffer.data = vabsptr(sceGuSwapBuffers());

    start = false;
}


void g2dAdd() {
    if (!obj_begin) return;
    if (obj.scale_w == 0 || obj.scale_h == 0) return;

    if (!(obj_list_size % MALLOC_STEP)) {
        obj_list = realloc(obj_list, (obj_list_size + MALLOC_STEP) * sizeof(Object));
    }

    obj_list_size++;
    if (obj_use_camera) {
        obj.x -= obj_camera_x;
        obj.y -= obj_camera_y;
    }
    obj.rot_x = obj.x;
    obj.rot_y = obj.y;
    CURRENT_OBJ = obj;

    // Coord mode stuff
    CURRENT_OBJ.x -= (obj_coord_mode == G2D_UP_RIGHT ||
        obj_coord_mode == G2D_DOWN_RIGHT ?
        CURRENT_OBJ.scale_w :
        (obj_coord_mode == G2D_CENTER ?
            CURRENT_OBJ.scale_w / 2 : 0));
    CURRENT_OBJ.y -= (obj_coord_mode == G2D_DOWN_LEFT ||
        obj_coord_mode == G2D_DOWN_RIGHT ?
        CURRENT_OBJ.scale_h :
        (obj_coord_mode == G2D_CENTER ?
            CURRENT_OBJ.scale_h / 2 : 0));

    // Alpha stuff
    CURRENT_OBJ.color = G2D_MODULATE(CURRENT_OBJ.color, 255, obj.alpha);
}


void g2dPush() {
    if (transform_stack_size >= TRANSFORM_STACK_MAX) return;
    transform_stack_size++;
    CURRENT_TRANSFORM.x = obj.x;
    CURRENT_TRANSFORM.y = obj.y;
    CURRENT_TRANSFORM.z = obj.z;
    CURRENT_TRANSFORM.rot = obj.rot;
    CURRENT_TRANSFORM.rot_sin = obj.rot_sin;
    CURRENT_TRANSFORM.rot_cos = obj.rot_cos;
    CURRENT_TRANSFORM.scale_w = obj.scale_w;
    CURRENT_TRANSFORM.scale_h = obj.scale_h;
}


// void g2dPop() {
//     if (transform_stack_size <= 0) return;
//     obj.x = CURRENT_TRANSFORM.x;
//     obj.y = CURRENT_TRANSFORM.y;
//     obj.z = CURRENT_TRANSFORM.z;
//     obj.rot = CURRENT_TRANSFORM.rot;
//     obj.rot_sin = CURRENT_TRANSFORM.rot_sin;
//     obj.rot_cos = CURRENT_TRANSFORM.rot_cos;
//     obj.scale_w = CURRENT_TRANSFORM.scale_w;
//     obj.scale_h = CURRENT_TRANSFORM.scale_h;
//     if (obj.rot != 0.f) obj_use_rot = true;
//     if (obj.z != 0.f) obj_use_z = true;
//     transform_stack_size--;
// }
void g2dPop() {
    if (transform_stack_size <= 0) return;
    
    // Сначала уменьшаем счетчик, потом обращаемся к элементу
    transform_stack_size--;
    
    if (transform_stack_size >= 0) {
        obj.x = transform_stack[transform_stack_size].x;
        obj.y = transform_stack[transform_stack_size].y;
        obj.z = transform_stack[transform_stack_size].z;
        obj.rot = transform_stack[transform_stack_size].rot;
        obj.rot_sin = transform_stack[transform_stack_size].rot_sin;
        obj.rot_cos = transform_stack[transform_stack_size].rot_cos;
        obj.scale_w = transform_stack[transform_stack_size].scale_w;
        obj.scale_h = transform_stack[transform_stack_size].scale_h;
        
        if (obj.rot != 0.f) obj_use_rot = true;
        if (obj.z != 0.f) obj_use_z = true;
    }
}

// * Coord functions *

void g2dResetCoord() {
    obj_coord_mode = DEFAULT_COORD_MODE;
    obj.x = DEFAULT_X;
    obj.y = DEFAULT_Y;
    obj.z = DEFAULT_Z;
}


void g2dSetCoordMode(g2dCoord_Mode mode) {
    if (mode > G2D_CENTER) return;
    obj_coord_mode = mode;
}


void g2dGetCoordXYZ(float *x, float *y, float *z) {
    if (x != NULL) *x = obj.x;
    if (y != NULL) *y = obj.y;
    if (z != NULL) *z = obj.z;
}


void g2dSetCoordXY(float x, float y) {
    obj.x = x * global_scale;
    obj.y = y * global_scale;
    obj.z = 0.f;
}


void g2dSetCoordXYZ(float x, float y, float z) {
    obj.x = x * global_scale;
    obj.y = y * global_scale;
    obj.z = z * global_scale;
    if (z != 0.f) obj_use_z = true;
}


void g2dSetCoordXYRelative(float x, float y) {
    float inc_x = x, inc_y = y;
    if (obj.rot_cos != 1.f) {
        inc_x = -obj.rot_sin * y + obj.rot_cos * x;
        inc_y = obj.rot_cos * y + obj.rot_sin * x;
    }
    obj.x += inc_x * global_scale;
    obj.y += inc_y * global_scale;
}


void g2dSetCoordXYZRelative(float x, float y, float z) {
    g2dSetCoordXYRelative(x, y);
    obj.z += z * global_scale;
    if (z != 0.f) obj_use_z = true;
}


void g2dSetCoordInteger(bool use) {
    obj_use_int = use;
}

// * Scale functions *

void g2dResetGlobalScale() {
    global_scale = 1.f;
}


void g2dResetScale() {
    if (obj_tex == NULL) {
        obj.scale_w = DEFAULT_SIZE;
        obj.scale_h = DEFAULT_SIZE;
    } else {
        obj.scale_w = obj_tex->w;
        obj.scale_h = obj_tex->h;
    }

    obj.scale_w *= global_scale;
    obj.scale_h *= global_scale;
}


void g2dGetGlobalScale(float *scale) {
    if (scale != NULL) *scale = global_scale;
}


void g2dGetScaleWH(float *w, float *h) {
    if (w != NULL) *w = obj.scale_w;
    if (h != NULL) *h = obj.scale_h;
}


void g2dSetGlobalScale(float scale) {
    global_scale = scale;
}


void g2dSetScale(float w, float h) {
    g2dResetScale();
    g2dSetScaleRelative(w, h);
}


void g2dSetScaleWH(float w, float h) {
    obj.scale_w = w * global_scale;
    obj.scale_h = h * global_scale;
    // A trick to prevent an unexpected behavior when mirroring with GU_SPRITES.
    if (obj.scale_w < 0 || obj.scale_h < 0) obj_use_rot = true;
}


void g2dSetScaleRelative(float w, float h) {
    obj.scale_w *= w;
    obj.scale_h *= h;

    if (obj.scale_w < 0 || obj.scale_h < 0) obj_use_rot = true;
}


void g2dSetScaleWHRelative(float w, float h) {
    obj.scale_w += w * global_scale;
    obj.scale_h += h * global_scale;

    if (obj.scale_w < 0 || obj.scale_h < 0) obj_use_rot = true;
}

// * Color functions *

void g2dResetColor() {
    obj.color = DEFAULT_COLOR;
}


void g2dResetAlpha() {
    obj.alpha = DEFAULT_ALPHA;
}


void g2dGetAlpha(g2dAlpha *alpha) {
    if (alpha != NULL) *alpha = obj.alpha;
}


void g2dSetColor(g2dColor color) {
    obj.color = color;
    if (++obj_colors_count > 1) obj_use_vert_color = true;
    if (G2D_GET_A(obj.color) < 255) obj_use_blend = true;
}


void g2dSetAlpha(g2dAlpha alpha) {
    if (alpha < 0) alpha = 0;
    if (alpha > 255) alpha = 255;
    obj.alpha = alpha;
    if (++obj_colors_count > 1) obj_use_vert_color = true;
    if (obj.alpha < 255) obj_use_blend = true;
}


void g2dSetAlphaRelative(int alpha) {
    g2dSetAlpha(obj.alpha + alpha);
}

// * Rotations functions *

void g2dResetRotation() {
    obj.rot = 0.f;
    obj.rot_sin = 0.f;
    obj.rot_cos = 1.f;
}


void g2dGetRotationRad(float *radians) {
    if (radians != NULL) *radians = obj.rot;
}


void g2dGetRotation(float *degrees) {
    if (degrees != NULL) *degrees = obj.rot * M_180_PI;
}


void g2dSetRotationRad(float radians) {
    if (radians == obj.rot) return;
    obj.rot = radians;
    obj.rot_sin = sinf(radians);
    obj.rot_cos = cosf(radians);
    if (radians != 0.f) obj_use_rot = true;
}


void g2dSetRotation(float degrees) {
    g2dSetRotationRad(degrees * M_PI_180);
}


void g2dSetRotationRadRelative(float radians) {
    g2dSetRotationRad(obj.rot + radians);
}


void g2dSetRotationRelative(float degrees) {
    g2dSetRotationRadRelative(degrees * M_PI_180);
}

// * Crop functions *

void g2dResetCrop() {
    if (obj_tex == NULL) return;
    obj.crop_x = 0;
    obj.crop_y = 0;
    obj.crop_w = obj_tex->w;
    obj.crop_h = obj_tex->h;
}


void g2dGetCropXY(int *x, int *y) {
    if (obj_tex == NULL) return;
    if (x != NULL) *x = obj.crop_x;
    if (y != NULL) *y = obj.crop_y;
}


void g2dGetCropWH(int *w, int *h) {
    if (obj_tex == NULL) return;
    if (w != NULL) *w = obj.crop_w;
    if (h != NULL) *h = obj.crop_h;
}


void g2dSetCropXY(int x, int y) {
    if (obj_tex == NULL) return;
    obj.crop_x = x;
    obj.crop_y = y;
}


void g2dSetCropWH(int w, int h) {
    if (obj_tex == NULL) return;
    obj.crop_w = w;
    obj.crop_h = h;
}


void g2dSetCropXYRelative(int x, int y) {
    if (obj_tex == NULL) return;
    g2dSetCropXY(obj.crop_x + x, obj.crop_y + y);
}


void g2dSetCropWHRelative(int w, int h) {
    if (obj_tex == NULL) return;
    g2dSetCropWH(obj.crop_w + w, obj.crop_h + h);
}

// * Texture functions *

void g2dResetTex() {
    if (obj_tex == NULL) return;
    obj_use_tex_repeat = false;
    obj_use_tex_linear = false;
    if (obj_tex->can_blend) obj_use_blend = true;
}


void g2dSetTexRepeat(bool use) {
    if (obj_tex == NULL) return;
    obj_use_tex_repeat = use;
}


void g2dSetTexBlend(bool use) {
    if (obj_tex == NULL) return;
    if (!obj_tex->can_blend) return;
    obj_use_blend = use;
}


void g2dSetTexLinear(bool use) {
    if (obj_tex == NULL) return;
    obj_use_tex_linear = use;
}

// * Texture management *

int _getNextPower2(int n) {
    int p = 1;
    while ((p <<= 1) < n);
    return p;
}

void _swizzle(unsigned char *dest, unsigned char *source, int width, int height) {
    int i, j;
    int rowblocks = (width / 16);
    int rowblocks_add = (rowblocks - 1) * 128;
    unsigned int block_address = 0;
    unsigned int *img = (unsigned int *)source;
    for (j = 0; j < height; j++, block_address += 16) {
        unsigned int *block = (unsigned int *)(dest + block_address);
        for (i = 0; i < rowblocks; i++) {
            *block++ = *img++;
            *block++ = *img++;
            *block++ = *img++;
            *block++ = *img++;
            block += 28;
        }
        if ((j & 0x7) == 0x7) block_address += rowblocks_add;
    }
}

// g2dImage *_g2dTexCreate(int w, int h, bool can_blend) {
//     g2dImage *tex = malloc(sizeof(g2dImage));
//     if (tex == NULL) return NULL;

//     tex->tw = _getNextPower2(w);
//     tex->th = _getNextPower2(h);
//     tex->w = w;
//     tex->h = h;
//     tex->ratio = (float)w / h;
//     tex->swizzled = false;
//     tex->can_blend = can_blend;
//     tex->format = G2D_RGBA8888; // По умолчанию RGBA
//     tex->palette = NULL;

//     // Выделяем память под RGBA (32 бита), так как загрузчики пишут сюда сначала
//     tex->data = malloc(tex->tw * tex->th * sizeof(g2dColor));
//     if (tex->data == NULL) { free(tex); return NULL; }
//     memset(tex->data, 0, tex->tw * tex->th * sizeof(g2dColor));

//     return tex;
// }
g2dImage *_g2dTexCreate(int w, int h, bool can_blend) {
    // Используем calloc вместо malloc для автоматической инициализации нулями
    g2dImage *tex = calloc(1, sizeof(g2dImage));
    if (tex == NULL) return NULL;

    tex->tw = _getNextPower2(w);
    tex->th = _getNextPower2(h);
    tex->w = w;
    tex->h = h;
    tex->ratio = (float)w / h;
    tex->swizzled = false;
    tex->can_blend = can_blend;
    tex->format = GU_PSM_8888; // Используем константу
    tex->palette = NULL;
    tex->data = NULL;

    // Выделяем память под RGBA
    tex->data = malloc(tex->tw * tex->th * sizeof(g2dColor));
    if (tex->data == NULL) { 
        free(tex); 
        return NULL; 
    }
    memset(tex->data, 0, tex->tw * tex->th * sizeof(g2dColor));

    return tex;
}


void g2dTexFree(g2dImage **tex) {
    if (tex == NULL || *tex == NULL) return;
    
    if ((*tex)->data) {
        free((*tex)->data);
        (*tex)->data = NULL;
    }
    
    if ((*tex)->palette) {
        free((*tex)->palette); // Очищаем палитру, если она была (для CLUT)
        (*tex)->palette = NULL;
    }
    
    free(*tex);
    *tex = NULL;
}


#ifdef USE_PNG
g2dImage *_g2dTexLoadPNG(FILE *fp) {
    png_structp png_ptr;
    png_infop info_ptr;
    unsigned int sig_read = 0;
    png_uint_32 width, height;
    int bit_depth, color_type, interlace_type;
    u32 x, y, *line;
    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_set_error_fn(png_ptr, NULL, NULL, NULL);
    info_ptr = png_create_info_struct(png_ptr);
    png_init_io(png_ptr, fp);
    png_set_sig_bytes(png_ptr, sig_read);
    png_read_info(png_ptr, info_ptr);
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type,
        &interlace_type, NULL, NULL);
    png_set_strip_16(png_ptr);
    png_set_packing(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);
    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);
    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);
    g2dImage *tex = _g2dTexCreate(width, height, true);
    line = malloc(width * 4);
    for (y = 0; y < height; y++) {
        png_read_row(png_ptr, (u8 *)line, NULL);
        for (x = 0; x < width; x++) {
            u32 color = line[x];
            ((g2dColor*)tex->data)[x + y * tex->tw] = color;
        }
    }
    free(line);
    png_read_end(png_ptr, info_ptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return tex;
}

void readData(png_structp png_ptr, png_bytep data, png_size_t length) {
    CIMGContext *ctx = (CIMGContext *)png_get_io_ptr(png_ptr);

    if (ctx->pos + length > ctx->size) {
        png_error(png_ptr, "Read beyond end of data");
    }

    memcpy(data, ctx->data + ctx->pos, length);
    ctx->pos += length;
}

g2dImage *_g2dTexLoadPNGfromC(const unsigned char *data, size_t size) {
    png_structp png_ptr;
    png_infop info_ptr;
    png_uint_32 width, height;
    int bit_depth, color_type, interlace_type;
    u32 x, y, *line;

    png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png_ptr) return NULL;

    info_ptr = png_create_info_struct(png_ptr);
    if (!info_ptr) {
        png_destroy_read_struct(&png_ptr, NULL, NULL);
        return NULL;
    }

    CIMGContext ctx = { data, size, 0 };
    png_set_read_fn(png_ptr, &ctx, readData);
    png_set_sig_bytes(png_ptr, 0);
    png_read_info(png_ptr, info_ptr);
    png_get_IHDR(png_ptr, info_ptr, &width, &height, &bit_depth, &color_type, &interlace_type, NULL, NULL);
    png_set_strip_16(png_ptr);
    png_set_packing(png_ptr);
    if (color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_palette_to_rgb(png_ptr);

    if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        png_set_tRNS_to_alpha(png_ptr);

    png_set_filler(png_ptr, 0xff, PNG_FILLER_AFTER);

    g2dImage *tex = _g2dTexCreate(width, height, true);
    line = malloc(width * 4);
    for (y = 0; y < height; y++) {
        png_read_row(png_ptr, (u8 *)line, NULL);
        for (x = 0; x < width; x++) {
            u32 color = line[x];
            ((g2dColor*)tex->data)[x + y * tex->tw] = color;
        }
    }
    free(line);
    png_read_end(png_ptr, info_ptr);
    png_destroy_read_struct(&png_ptr, &info_ptr, NULL);

    return tex;
}
#endif

#ifdef USE_BMP
g2dImage *_g2dTexLoadBMP(FILE *fp) {
    BMPHeader header;
    fread(&header, sizeof(BMPHeader), 1, fp);

    if (header.signature != 0x4D42)
        return NULL;


    if (header.bitsPerPixel != 24 && header.bitsPerPixel != 32)
        return NULL;


    g2dImage *tex = _g2dTexCreate(header.width, abs(header.height), true);
    if (!tex) return NULL;

    fseek(fp, header.dataOffset, SEEK_SET);

    int rowSize = ((header.width * header.bitsPerPixel + 31) / 32) * 4;
    u8 *rowData = malloc(rowSize);
    int y;

    for (y = 0; y < abs(header.height); y++) {
        fread(rowData, 1, rowSize, fp);
        u8 *pixel = rowData;
        int x;

        for (x = 0; x < header.width; x++) {
            u32 color = (pixel[0] << 16) | (pixel[1] << 8) | pixel[2] | 0xFF000000;
            if (header.bitsPerPixel == 32)
                pixel += 4;
            else // 24-bit
                pixel += 3;

            int targetY = (header.height > 0) ? (header.height - 1 - y) : y;
            ((g2dColor*)tex->data)[x + targetY * tex->tw] = color;
        }
    }

    free(rowData);
    return tex;
}

g2dImage *_g2dTexLoadBMPfromC(const unsigned char *data, size_t size) {
    CIMGContext ctx = { data, size, 0 };

    if (ctx.size - ctx.pos < 54) return NULL;

    u16 signature = *(u16 *)(ctx.data + ctx.pos);
    ctx.pos += 2;
    if (signature != 0x4D42) return NULL;

    ctx.pos += 8;

    u32 dataOffset = *(u32 *)(ctx.data + ctx.pos);
    ctx.pos += 4;

    u32 headerSize = *(u32 *)(ctx.data + ctx.pos);
    ctx.pos += 4;
    if (headerSize < 40) return NULL;

    u32 width = *(u32 *)(ctx.data + ctx.pos);
    ctx.pos += 4;
    u32 height = *(u32 *)(ctx.data + ctx.pos);
    ctx.pos += 4;

    u16 planes = *(u16 *)(ctx.data + ctx.pos);
    ctx.pos += 2;
    if (planes != 1) return NULL;

    u16 bitsPerPixel = *(u16 *)(ctx.data + ctx.pos);
    ctx.pos += 2;
    if (bitsPerPixel != 24 && bitsPerPixel != 32) return NULL;

    ctx.pos = 54;

    g2dImage *tex = _g2dTexCreate(width, abs(height), true);
    if (!tex) return NULL;

    ctx.pos = dataOffset;

    int rowSize = ((width * bitsPerPixel + 31) / 32) * 4;
    int y;

    for (y = 0; y < abs(height); y++) {
        if (ctx.pos + rowSize > ctx.size) {
            g2dTexFree(&tex);
            return NULL;
        }

        const u8 *rowData = ctx.data + ctx.pos;
        ctx.pos += rowSize;

        const u8 *pixel = rowData;
        int x;

        for (x = 0; x < width; x++) {
            u32 color = (pixel[0] << 16) | (pixel[1] << 8) | pixel[2] | 0xFF000000;
            if (bitsPerPixel == 32)
                pixel += 4;
            else // 24-bit
                pixel += 3;

            int targetY = (height > 0) ? (height - 1 - y) : y;
            ((g2dColor*)tex->data)[x + targetY * tex->tw] = color;
        }
    }

    return tex;
}
#endif

#ifdef USE_TGA
g2dImage *_g2dTexLoadTGA(FILE *fp) {
    TGAHeader header;
    if (fread(&header, sizeof(TGAHeader), 1, fp) != 1)  return NULL;

    if (header.imageType != 2 && header.imageType != 10) return NULL;

    if (header.bpp != 24 && header.bpp != 32) return NULL;

    fseek(fp, header.idLength, SEEK_CUR);

    g2dImage *tex = _g2dTexCreate(header.width, header.height, true);
    if (!tex) return NULL;

    int pixelSize = header.bpp / 8;
    int imageSize = header.width * header.height * pixelSize;
    u8 *imageData = malloc(imageSize);
    if (!imageData) {
        g2dTexFree(&tex);
        return NULL;
    }

    if (header.imageType == 2)
        fread(imageData, 1, imageSize, fp);
    else if (header.imageType == 10) { // RLE-сжатие
        u8 *ptr = imageData;
        int remaining = imageSize;

        while (remaining > 0) {
            u8 chunkHeader;
            if (fread(&chunkHeader, 1, 1, fp) != 1) break;

            int chunkSize = (chunkHeader & 0x7F) + 1;
            int chunkBytes = chunkSize * pixelSize;

            if (chunkHeader & 0x80) { // RLE-пакет
                u8 pixel[4];
                if (fread(pixel, 1, pixelSize, fp) != pixelSize) break;

                for (int i = 0; i < chunkSize; i++) {
                    if (remaining < pixelSize) break;
                    memcpy(ptr, pixel, pixelSize);
                    ptr += pixelSize;
                    remaining -= pixelSize;
                }
            } else { // RAW-пакет
                int readBytes = chunkBytes < remaining ? chunkBytes : remaining;
                if (fread(ptr, 1, readBytes, fp) != readBytes) break;
                ptr += readBytes;
                remaining -= readBytes;
            }
        }
    }

    // Обработка пиксельных данных
    u8 *pixel = imageData;
    for (int y = 0; y < header.height; y++) {
        int targetY = (header.descriptor & 0x20) ? y : (header.height - 1 - y);

        for (int x = 0; x < header.width; x++) {
            u32 color;
            if (header.bpp == 32)
                color = pixel[2] | (pixel[1] << 8) | (pixel[0] << 16) | (pixel[3] << 24);
            else // 24-bit
                color = pixel[2] | (pixel[1] << 8) | (pixel[0] << 16) | 0xFF000000;

            ((g2dColor*)tex->data)[x + targetY * tex->tw] = color;
            pixel += pixelSize;
        }
    }

    free(imageData);
    return tex;
}

g2dImage *_g2dTexLoadTGAfromC(const unsigned char *data, size_t size) {
    CIMGContext ctx = { data, size, 0 };

    if (ctx.size - ctx.pos < 18) return NULL;

    u8 idLength = ctx.data[ctx.pos++];
    u8 imageType = ctx.data[ctx.pos++];

    ctx.pos += 5;

    u16 width = *(u16 *)(ctx.data + ctx.pos);
    ctx.pos += 2;
    u16 height = *(u16 *)(ctx.data + ctx.pos);
    ctx.pos += 2;

    u8 bpp = ctx.data[ctx.pos++];
    u8 descriptor = ctx.data[ctx.pos++];

    if (imageType != 2 && imageType != 10) return NULL;

    if (bpp != 24 && bpp != 32) return NULL;

    ctx.pos += idLength;

    g2dImage *tex = _g2dTexCreate(width, height, true);
    if (!tex) return NULL;

    int pixelSize = bpp / 8;
    int imageSize = width * height * pixelSize;

    if (imageType == 2) {
        if (ctx.pos + imageSize > ctx.size) {
            g2dTexFree(&tex);
            return NULL;
        }

        const u8 *imageData = ctx.data + ctx.pos;
        ctx.pos += imageSize;

        const u8 *pixel = imageData;
        for (int y = 0; y < height; y++) {
            int targetY = (descriptor & 0x20) ? y : (height - 1 - y);

            for (int x = 0; x < width; x++) {
                u32 color;
                if (bpp == 32)
                    color = pixel[2] | (pixel[1] << 8) | (pixel[0] << 16) | (pixel[3] << 24);
                else // 24-bit
                    color = pixel[2] | (pixel[1] << 8) | (pixel[0] << 16) | 0xFF000000;

                ((g2dColor*)tex->data)[x + targetY * tex->tw] = color;
                pixel += pixelSize;
            }
        }
    } else if (imageType == 10) {
        u8 *imageData = malloc(imageSize);
        if (!imageData) {
            g2dTexFree(&tex);
            return NULL;
        }

        u8 *ptr = imageData;
        int remaining = imageSize;

        while (remaining > 0 && ctx.pos < ctx.size) {
            if (ctx.pos >= ctx.size) break;

            u8 chunkHeader = ctx.data[ctx.pos++];
            int chunkSize = (chunkHeader & 0x7F) + 1;
            int chunkBytes = chunkSize * pixelSize;

            if (chunkHeader & 0x80) {
                if (ctx.pos + pixelSize > ctx.size) break;

                u8 pixel[4];
                memcpy(pixel, ctx.data + ctx.pos, pixelSize);
                ctx.pos += pixelSize;

                for (int i = 0; i < chunkSize; i++) {
                    if (remaining < pixelSize) break;
                    memcpy(ptr, pixel, pixelSize);
                    ptr += pixelSize;
                    remaining -= pixelSize;
                }
            } else {
                int readBytes = (chunkBytes < remaining) ? chunkBytes : remaining;
                if (ctx.pos + readBytes > ctx.size) break;

                memcpy(ptr, ctx.data + ctx.pos, readBytes);
                ptr += readBytes;
                ctx.pos += readBytes;
                remaining -= readBytes;
            }
        }


        u8 *pixel = imageData;
        for (int y = 0; y < height; y++) {
            int targetY = (descriptor & 0x20) ? y : (height - 1 - y);

            for (int x = 0; x < width; x++) {
                u32 color;
                if (bpp == 32)
                    color = pixel[2] | (pixel[1] << 8) | (pixel[0] << 16) | (pixel[3] << 24);
                else  // 24-bit
                    color = pixel[2] | (pixel[1] << 8) | (pixel[0] << 16) | 0xFF000000;

                ((g2dColor*)tex->data)[x + targetY * tex->tw] = color;
                pixel += pixelSize;
            }
        }

        free(imageData);
    }

    return tex;
}
#endif

#include <malloc.h>

// Поиск или добавление цвета в палитру (простейшее квантование)
static int _get_or_add_palette_color(g2dColor color, g2dColor *palette, int *pal_count, int max_colors) {
    for (int i = 0; i < *pal_count; i++) {
        if (palette[i] == color) return i;
    }
    if (*pal_count < max_colors) {
        palette[*pal_count] = color;
        return (*pal_count)++;
    }
    return 0; // Возвращаем 0, если палитра переполнена
}

// Универсальный свайзлинг для 32, 8 и 4 бит
// static void _g2dSwizzle(g2dImage *tex) {
//     int width_in_bytes = 0;
//     if (tex->format == GU_PSM_8888)      width_in_bytes = tex->tw * 4;
//     else if (tex->format == GU_PSM_T8)   width_in_bytes = tex->tw;
//     else if (tex->format == GU_PSM_T4)   width_in_bytes = tex->tw / 2;
//     else return; // Неизвестный формат

//     if (width_in_bytes < 16) return; // Слишком узкая для свайзла

//     unsigned char *tmp = (unsigned char *)malloc(width_in_bytes * tex->th);
//     if (!tmp) return;

//     unsigned char *in = (unsigned char *)tex->data;
//     int row_blocks = width_in_bytes / 16;

//     for (int j = 0; j < tex->th; j++) {
//         for (int i = 0; i < row_blocks; i++) {
//             int blockx = i;
//             int blocky = j / 8;
//             int y = j % 8;
//             unsigned char *dest = tmp + (blocky * row_blocks * 128) + (blockx * 128) + (y * 16);
//             memcpy(dest, in + (j * width_in_bytes) + (i * 16), 16);
//         }
//     }

//     free(tex->data);
//     tex->data = (g2dColor *)tmp;
//     tex->swizzled = true;
// }
static void _g2dSwizzle(g2dImage *tex) {
    int width_in_bytes = 0;
    if (tex->format == GU_PSM_8888) {
        width_in_bytes = tex->tw * 4;
    } else if (tex->format == GU_PSM_T8) {
        width_in_bytes = tex->tw;
    } else if (tex->format == GU_PSM_T4) {
        width_in_bytes = tex->tw / 2;
    } else {
        return; // Неизвестный формат
    }

    if (width_in_bytes < 16) return; // Слишком узкая для свайзла

    unsigned char *tmp = (unsigned char *)malloc(width_in_bytes * tex->th);
    if (!tmp) {
        // Логируем ошибку, но не прерываем выполнение
        #ifdef DEBUG
        printf("_g2dSwizzle: Failed to allocate memory\n");
        #endif
        return;
    }

    unsigned char *in = (unsigned char *)tex->data;
    int row_blocks = width_in_bytes / 16;

    for (int j = 0; j < tex->th; j++) {
        for (int i = 0; i < row_blocks; i++) {
            int blockx = i;
            int blocky = j / 8;
            int y = j % 8;
            unsigned char *dest = tmp + (blocky * row_blocks * 128) + (blockx * 128) + (y * 16);
            memcpy(dest, in + (j * width_in_bytes) + (i * 16), 16);
        }
    }

    free(tex->data);
    tex->data = (g2dColor *)tmp;
    tex->swizzled = true;
}

// Конвертация временного буфера RGBA в индексированный формат
// static void _g2dApplyFormat(g2dImage *tex, g2dColor *rgba_buffer, int target_hw_format) {
//     int total_pixels = tex->tw * tex->th;
//     tex->format = target_hw_format;

//     // Освобождаем старые данные, если они были выделены в _g2dTexCreate
//     if (tex->data) free(tex->data);

//     if (target_hw_format == 3) { // RGBA 8888
//         tex->data = malloc(total_pixels * 4);
//         if (tex->data) memcpy(tex->data, rgba_buffer, total_pixels * 4);
//         tex->palette = NULL;
//     } 
//     else {
//         // Палитра ОБЯЗАТЕЛЬНО выровнена по 16 байт
//         tex->palette = (g2dColor *)memalign(16, 256 * sizeof(g2dColor));
//         memset(tex->palette, 0, 256 * sizeof(g2dColor));
        
//         int pal_count = 0;
//         if (target_hw_format == 5) { // T8 (8-bit)
//             unsigned char *indices = (unsigned char *)malloc(total_pixels);
//             for (int i = 0; i < total_pixels; i++) {
//                 indices[i] = (unsigned char)_get_or_add_palette_color(rgba_buffer[i], tex->palette, &pal_count, 256);
//             }
//             tex->data = (void *)indices;
//         } 
//         else if (target_hw_format == 4) { // T4 (4-bit)
//             unsigned char *indices = (unsigned char *)malloc(total_pixels / 2);
//             memset(indices, 0, total_pixels / 2);
//             for (int i = 0; i < total_pixels; i++) {
//                 int idx = _get_or_add_palette_color(rgba_buffer[i], tex->palette, &pal_count, 16);
//                 if (i % 2 == 0) indices[i/2] |= (idx & 0x0F);
//                 else            indices[i/2] |= (idx << 4);
//             }
//             tex->data = (void *)indices;
//         }
//     }
//     // Сбрасываем кэш сразу после изменения данных
//     sceKernelDcacheWritebackAll();
// }

static void _g2dApplyFormat(g2dImage *tex, g2dColor *rgba_buffer, int target_hw_format) {
    int total_pixels = tex->tw * tex->th;
    tex->format = target_hw_format;

    // Освобождаем старые данные tex->data и сразу зануляем указатель
    if (tex->data) {
        free(tex->data);
        tex->data = NULL;
    }
    
    // НЕ ОСВОБОЖДАЕМ rgba_buffer здесь, он нам еще нужен!

    if (target_hw_format == GU_PSM_8888) {
        tex->data = malloc(total_pixels * 4);
        if (!tex->data) {
            free(rgba_buffer);
            rgba_buffer = NULL;
            return;
        }
        // Теперь rgba_buffer точно существует и мы можем его копировать
        memcpy(tex->data, rgba_buffer, total_pixels * 4);
        tex->palette = NULL;
        // Освобождаем rgba_buffer после использования
        free(rgba_buffer);
        rgba_buffer = NULL;
    } 
    else {
        // Палитра ОБЯЗАТЕЛЬНО выровнена по 16 байт
        tex->palette = (g2dColor *)memalign(16, 256 * sizeof(g2dColor));
        if (!tex->palette) {
            free(rgba_buffer);
            rgba_buffer = NULL;
            return;
        }
        memset(tex->palette, 0, 256 * sizeof(g2dColor));
        
        int pal_count = 0;
        if (target_hw_format == GU_PSM_T8) {
            unsigned char *indices = (unsigned char *)malloc(total_pixels);
            if (!indices) {
                free(tex->palette);
                tex->palette = NULL;
                free(rgba_buffer);
                rgba_buffer = NULL;
                return;
            }
            for (int i = 0; i < total_pixels; i++) {
                indices[i] = (unsigned char)_get_or_add_palette_color(rgba_buffer[i], tex->palette, &pal_count, 256);
            }
            tex->data = (void *)indices;
        } 
        else if (target_hw_format == GU_PSM_T4) {
            unsigned char *indices = (unsigned char *)malloc(total_pixels / 2);
            if (!indices) {
                free(tex->palette);
                tex->palette = NULL;
                free(rgba_buffer);
                rgba_buffer = NULL;
                return;
            }
            memset(indices, 0, total_pixels / 2);
            for (int i = 0; i < total_pixels; i++) {
                int idx = _get_or_add_palette_color(rgba_buffer[i], tex->palette, &pal_count, 16);
                if (i % 2 == 0) indices[i/2] |= (idx & 0x0F);
                else            indices[i/2] |= (idx << 4);
            }
            tex->data = (void *)indices;
        }
        
        // Освобождаем rgba_buffer после использования для CLUT форматов
        free(rgba_buffer);
        rgba_buffer = NULL;
    }
    
    // Сбрасываем кэш сразу после изменения данных
    sceKernelDcacheWritebackAll();
}

// g2dImage *g2dTexLoad(char path[], unsigned char *data, size_t size, g2dTex_Mode mode) {
//     // 1. Извлекаем выбранный формат из аргумента mode
//     // Очищаем mode от бита свайзлинга (128), чтобы получить чистый ID формата
//     int user_fmt = (mode & ~G2D_SWIZZLE);
//     int hw_format;

//     // ПРАВИЛЬНЫЙ МАППИНГ: Lua ID -> Константы PSP GU
//     if (user_fmt == G2D_LUA_CLUT8) {
//         hw_format = 5; // GU_PSM_T8
//     } else if (user_fmt == G2D_LUA_CLUT4) {
//         hw_format = 4; // GU_PSM_T4
//     } else {
//         hw_format = 3; // GU_PSM_8888 (Сюда попадет и 0/G2D_VOID)
//     }

//     bool use_swizzle = (mode & G2D_SWIZZLE);
//     g2dImage *temp_tex = NULL;

//     // 2. ЗАГРУЗКА: Сначала всегда загружаем файл в полноцветном режиме (RGBA)
//     if (path == NULL && data == NULL) return NULL;

//     if (path == NULL) { // Загрузка из памяти (C-массива)
//         if (size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
// #ifdef USE_PNG
//             temp_tex = _g2dTexLoadPNGfromC(data, size);
// #endif
//         } else if (size >= 2 && data[0] == 0xFF && data[1] == 0xD8) {
//         }
//     } else { // Загрузка из файла по пути
//         FILE *fp = fopen(path, "rb");
//         if (fp == NULL) return NULL;

//         const char *ext = strrchr(path, '.');
//         if (ext != NULL) {
//             ext++; 
// #ifdef USE_PNG
//             if (strcasecmp(ext, "png") == 0) temp_tex = _g2dTexLoadPNG(fp);
// #endif
//         }
//         fclose(fp);
//     }

//     // Если ни один загрузчик не сработал
//     if (temp_tex == NULL) return NULL;

//     // 3. СОЗДАНИЕ ФИНАЛЬНОЙ СТРУКТУРЫ
//     g2dImage *tex = (g2dImage *)malloc(sizeof(g2dImage));
//     if (tex == NULL) {
//         g2dTexFree(&temp_tex);
//         return NULL;
//     }
    
//     // КРИТИЧЕСКИ ВАЖНО: Обнуляем всё, чтобы swizzled был 0 по умолчанию
//     memset(tex, 0, sizeof(g2dImage));

//     tex->w = temp_tex->w;
//     tex->h = temp_tex->h;
//     tex->tw = temp_tex->tw;
//     tex->th = temp_tex->th;
//     tex->ratio = temp_tex->ratio;
//     tex->can_blend = temp_tex->can_blend;

//     // 4. ПРИМЕНЕНИЕ ФОРМАТА (Конвертация RGBA -> CLUT или простое копирование)
//     // Мы передаем данные из temp_tex->data в нашу функцию обработки
//     _g2dApplyFormat(tex, (g2dColor*)temp_tex->data, hw_format);

//     // Удаляем временную текстуру (RGBA данные больше не нужны)
//     g2dTexFree(&temp_tex);

//     // Проверка лимитов железа
//     if (tex->tw > 512 || tex->th > 512) {
//         g2dTexFree(&tex);
//         return NULL;
//     }

//     // 5. СВАЙЗЛИНГ (если запрошен и ширина позволяет)
//     // Ширина в байтах должна быть кратна 16 для свайзлинга
//     if (use_swizzle && (tex->tw >= 16)) {
//         _g2dSwizzle(tex);
//     }

//     // 6. СИНХРОНИЗАЦИЯ КЭША: Чтобы GPU увидел данные в ОЗУ
//     sceKernelDcacheWritebackAll();

//     return tex;
// }
g2dImage *g2dTexLoad(const char *path, unsigned char *data, size_t size, g2dTex_Mode mode) {
    int user_fmt = (mode & ~G2D_SWIZZLE);
    int hw_format;

    if (user_fmt == G2D_LUA_CLUT8) {
        hw_format = GU_PSM_T8;
    } else if (user_fmt == G2D_LUA_CLUT4) {
        hw_format = GU_PSM_T4;
    } else {
        hw_format = GU_PSM_8888;
    }

    bool use_swizzle = (mode & G2D_SWIZZLE);
    g2dImage *temp_tex = NULL;

    // Загрузка изображения
    if (path == NULL && data == NULL) return NULL;

    if (path == NULL) {
        if (size >= 8 && memcmp(data, "\x89PNG\r\n\x1a\n", 8) == 0) {
#ifdef USE_PNG
            temp_tex = _g2dTexLoadPNGfromC(data, size);
#endif
        }
    } else {
        FILE *fp = fopen(path, "rb");
        if (fp == NULL) return NULL;

        const char *ext = strrchr(path, '.');
        if (ext != NULL) {
            ext++; 
#ifdef USE_PNG
            if (strcasecmp(ext, "png") == 0) temp_tex = _g2dTexLoadPNG(fp);
#endif
        }
        fclose(fp);
    }

        if (temp_tex == NULL) return NULL;

    g2dImage *tex = (g2dImage *)calloc(1, sizeof(g2dImage));
    if (tex == NULL) {
        g2dTexFree(&temp_tex);
        return NULL;
    }

    tex->w = temp_tex->w;
    tex->h = temp_tex->h;
    tex->tw = temp_tex->tw;
    tex->th = temp_tex->th;
    tex->ratio = temp_tex->ratio;
    tex->can_blend = temp_tex->can_blend;

    // _g2dApplyFormat теперь сама освобождает rgba_buffer
    _g2dApplyFormat(tex, (g2dColor*)temp_tex->data, hw_format);
    
    // temp_tex->data теперь недействителен, так как _g2dApplyFormat его освободил
    // Поэтому зануляем указатель, чтобы g2dTexFree не пытался освободить его снова
    temp_tex->data = NULL;

    // Проверяем, что конвертация прошла успешно
    if (!tex->data) {
        g2dTexFree(&tex);
        g2dTexFree(&temp_tex);
        return NULL;
    }

    g2dTexFree(&temp_tex);

    if (tex->tw > 512 || tex->th > 512) {
        g2dTexFree(&tex);
        return NULL;
    }

    if (use_swizzle && (tex->tw >= 16)) {
        _g2dSwizzle(tex);
    }

    sceKernelDcacheWritebackAll();
    return tex;
}

g2dImage *g2dTexCreatePlaceholder() {
    // Create a 64x64 image with alpha blending support
    g2dImage *tex = _g2dTexCreate(64, 64, true);
    if (tex == NULL) return NULL;

    // Define the colors
    g2dColor dark_gray = G2D_DARKGRAY;    // 0xFF3F3F3F
    g2dColor light_gray = G2D_LITEGRAY;   // 0xFFBFBFBF

    // Size of each checkerboard square
    int square_size = 8;

    // Fill the texture with checkerboard pattern
    for (int y = 0; y < tex->h; y++) {
        for (int x = 0; x < tex->w; x++) {
            // Determine which square we're in
            int square_x = x / square_size;
            int square_y = y / square_size;

            // Use alternating colors based on square position
            // Creates a chessboard pattern
            // Внутри циклов создания плейсхолдера
            if ((square_x + square_y) % 2 == 0) {
                ((g2dColor*)tex->data)[x + y * tex->tw] = dark_gray;
            } else {
                ((g2dColor*)tex->data)[x + y * tex->tw] = light_gray;
            }
        }
    }

    // Write back to cache for performance
    sceKernelDcacheWritebackAll();

    return tex;
}

// * Scissor functions *

void g2dResetScissor() {
    g2dSetScissor(0, 0, G2D_SCR_W, G2D_SCR_H);
    scissor = false;
}


void g2dSetScissor(int x, int y, int w, int h) {
    sceGuScissor(x, y, w, h);
    scissor = true;
}

void set_pixel(g2dImage *tex, int x, int y, g2dColor color) {
    if (x >= 0 && x < tex->w && y >= 0 && y < tex->h) {
        // Приводим к g2dColor*, так как функция принимает 32-битный цвет
        ((g2dColor*)tex->data)[x + y * tex->tw] = color;
    }
}
g2dColor get_pixel(g2dImage *tex, int x, int y) {
    if (x >= 0 && x < tex->w && y >= 0 && y < tex->h) {
        // Если текстура в CLUT режиме, обычное чтение вернет мусор, 
        // но для исправления ошибки компиляции пишем так:
        return ((g2dColor*)tex->data)[x + y * tex->tw];
    }
    return 0;
}

void draw_circle(g2dImage *tex, int x, int y, int w, int h, g2dColor color) {
    for (int i = 0; i < w; i++) {
        set_pixel(tex, x + i, y, color);         // верхняя линия
        set_pixel(tex, x + i, y + h - 1, color); // нижняя линия
    }
    for (int i = 0; i < h; i++) {
        set_pixel(tex, x, y + i, color);         // левая линия
        set_pixel(tex, x + w - 1, y + i, color); // правая линия
    }
    sceKernelDcacheWritebackAll();
}

// Простейший алгоритм: ищет цвет в палитре или добавляет его
static int _get_palette_index(g2dColor color, g2dColor *palette, int *pal_size, int max_colors) {
    for (int i = 0; i < *pal_size; i++) {
        if (palette[i] == color) return i;
    }
    if (*pal_size < max_colors) {
        palette[*pal_size] = color;
        return (*pal_size)++;
    }
    return 0; // Если цветов больше, чем лимит, возвращаем 0 индекс
}

// void _g2dConvertRGBAtoCLUT(g2dImage *tex, g2dColor *rgba_data, int format) {
//     //int max_colors = (format == G2D_CLUT8) ? 256 : 16;
//     // Палитра должна быть выровнена по 16 байт для PSP GU
//     tex->palette = memalign(16, 256 * sizeof(g2dColor));
//     memset(tex->palette, 0, 256 * sizeof(g2dColor));
    
//     int pal_count = 0;
//     int total_pixels = tex->tw * tex->th;

//     if (format == G2D_CLUT8) {
//         u8 *clut_data = malloc(total_pixels);
//         for (int i = 0; i < total_pixels; i++) {
//             clut_data[i] = (u8)_get_palette_index(rgba_data[i], tex->palette, &pal_count, 256);
//         }
//         tex->data = clut_data;
//     } else if (format == G2D_CLUT4) {
//         u8 *clut_data = malloc(total_pixels / 2);
//         memset(clut_data, 0, total_pixels / 2);
//         for (int i = 0; i < total_pixels; i++) {
//             int idx = _get_palette_index(rgba_data[i], tex->palette, &pal_count, 16);
//             if (i % 2 == 0) clut_data[i/2] |= (idx & 0x0F);
//             else            clut_data[i/2] |= (idx << 4);
//         }
//         tex->data = clut_data;
//     }
//     free(rgba_data);
// }
void _g2dConvertRGBAtoCLUT(g2dImage *tex, g2dColor *rgba_data, int format) {
    // Палитра должна быть выровнена по 16 байт для PSP GU
    tex->palette = memalign(16, 256 * sizeof(g2dColor));
    if (!tex->palette) {
        // Обработка ошибки - освобождаем rgba_data и выходим
        free(rgba_data);
        return;
    }
    memset(tex->palette, 0, 256 * sizeof(g2dColor));
    
    int pal_count = 0;
    int total_pixels = tex->tw * tex->th;

    if (format == G2D_CLUT8) {
        u8 *clut_data = malloc(total_pixels);
        if (!clut_data) {
            free(tex->palette);
            tex->palette = NULL;
            free(rgba_data);
            return;
        }
        for (int i = 0; i < total_pixels; i++) {
            clut_data[i] = (u8)_get_palette_index(rgba_data[i], tex->palette, &pal_count, 256);
        }
        tex->data = clut_data;
    } else if (format == G2D_CLUT4) {
        u8 *clut_data = malloc(total_pixels / 2);
        if (!clut_data) {
            free(tex->palette);
            tex->palette = NULL;
            free(rgba_data);
            return;
        }
        memset(clut_data, 0, total_pixels / 2);
        for (int i = 0; i < total_pixels; i++) {
            int idx = _get_palette_index(rgba_data[i], tex->palette, &pal_count, 16);
            if (i % 2 == 0) clut_data[i/2] |= (idx & 0x0F);
            else            clut_data[i/2] |= (idx << 4);
        }
        tex->data = clut_data;
    }
    free(rgba_data);
}

#ifdef __cplusplus
}
#endif

// EOF

