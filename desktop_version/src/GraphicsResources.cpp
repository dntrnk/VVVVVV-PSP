#include "GraphicsResources.h"

#include <pspkernel.h>
#include <pspgu.h>
#include <malloc.h>
#include <cassert>

#include <inttypes.h>
#include <time.h>
#include <tinyxml2.h>

#include <png.h>
#include <zlib.h>

#include "Alloc.h"
#include "FileSystemUtils.h"
#include "Graphics.h"
#include "GraphicsUtil.h"
#include "Localization.h"
#include "Vlogging.h"
#include "Screen.h"
#include "XMLUtils.h"

uint32_t sprites_collision_surface_normal[512][16] = {0};
uint32_t sprites_collision_surface_flipped[512][16] = {0};

bool sprites_collision_surface_get_bit(uint32_t (*collision_surface)[16], int x, int y)
{
    return (collision_surface[y][x >> 5] >> (x & 31)) & 1u;
}

void sprites_collision_surface_set_bit(uint32_t (*collision_surface)[16], int x, int y)
{
    collision_surface[y][x >> 5] |= (1u << (x & 31));
}

void sprites_collision_surface_clear_bit(uint32_t (*collision_surface)[16], int x, int y)
{
    collision_surface[y][x >> 5] &= ~(1u << (x & 31));
}

static int _get_or_add_palette_color(g2dColor color, g2dColor *palette, int *pal_count, int max_colors);
static void _g2dApplyFormat(g2dImage *tex, g2dColor *rgba_buffer, int target_hw_format);
static void _g2dSwizzle(g2dImage *tex);
static int _g2dPaletteLookup(g2dColor c, g2dColor *palette, int count, int max_colors);

// Used to load PNG data
extern "C"
{
    extern unsigned lodepng_decode32(
        unsigned char** out,
        unsigned* w,
        unsigned* h,
        const unsigned char* in,
        size_t insize
    );
    extern unsigned lodepng_inspect(
        unsigned* w, unsigned* h,
        void* state,
        const unsigned char* in, size_t insize
    );
    extern unsigned lodepng_encode24(
        unsigned char** out,
        size_t* outsize,
        const unsigned char* image,
        unsigned w,
        unsigned h
    );
    extern const char* lodepng_error_text(unsigned code);
}

typedef struct {
    const unsigned char* data;
    size_t size;
    size_t pos;
} PNGMemReader;

static void _png_mem_read(png_structp png, png_bytep out, png_size_t len)
{
    PNGMemReader* ctx = (PNGMemReader*)png_get_io_ptr(png);
    if (ctx->pos + len > ctx->size) {
        png_error(png, "read past end of data");
        return;
    }
    memcpy(out, ctx->data + ctx->pos, len);
    ctx->pos += len;
}

static void _png_error_dummy(png_structp, png_const_charp) {}
static void _png_warn_dummy(png_structp, png_const_charp) {}

#define GR_MAX_TEX_SIZE 512

static g2dImage* _G2DLoadTiledFromPNG(const unsigned char* fileData, size_t fileSize, const char* filename, TextureLoadType loadtype, int hw_format)
{
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, _png_error_dummy, _png_warn_dummy);
    if (!png) return NULL;
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); return NULL; }

    PNGMemReader reader1 = { fileData, fileSize, 0 };
    png_set_read_fn(png, &reader1, _png_mem_read);
    png_read_info(png, info);

    png_uint_32 width = png_get_image_width(png, info);
    png_uint_32 height = png_get_image_height(png, info);
    int bit_depth = png_get_bit_depth(png, info);
    int color_type = png_get_color_type(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    int max_colors = (hw_format == GU_PSM_T8) ? 256 : 16;

    g2dColor* palette = (g2dColor*)memalign(16, 512 * sizeof(g2dColor));
    if (!palette) {
        png_destroy_read_struct(&png, &info, NULL);
        return NULL;
    }
    memset(palette, 0, 512 * sizeof(g2dColor));

    int pal_count = 0;
    bool overflow = false;

    size_t rowbytes = width * 4;
    unsigned char* row = (unsigned char*)malloc(rowbytes);
    if (!row) {
        free(palette);
        png_destroy_read_struct(&png, &info, NULL);
        return NULL;
    }

    for (png_uint_32 y = 0; y < height; y++) {
        png_read_row(png, row, NULL);
        g2dColor* pixels = (g2dColor*)row;

        for (png_uint_32 x = 0; x < width; x++) {
            g2dColor c = pixels[x];

            if (loadtype == TEX_WHITE) {
                c = G2D_RGBA(255, 255, 255, G2D_GET_A(c));
            } else if (loadtype == TEX_GRAYSCALE) {
                Uint8 r = G2D_GET_R(c) * 0.299;
                Uint8 g = G2D_GET_G(c) * 0.587;
                Uint8 b = G2D_GET_B(c) * 0.114;
                const double gray = floor(r + g + b + 0.5);
                c = G2D_RGBA(gray, gray, gray, G2D_GET_A(c));
            }

            int found = -1;
            for (int j = 0; j < pal_count; j++) {
                if (palette[j] == c) { found = j; break; }
            }
            if (found >= 0) continue;
            if (pal_count < max_colors) {
                palette[pal_count++] = c;
            } else {
                overflow = true;
                goto pass1_done;
            }
        }
    }

pass1_done:
    free(row);
    png_destroy_read_struct(&png, &info, NULL);

    if (overflow) {
        int shift = 1;
        for (; shift <= 4; shift++) {
            int mask = 0xFF & ~((1 << shift) - 1);

            png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, _png_error_dummy, _png_warn_dummy);
            info = png_create_info_struct(png);
            PNGMemReader r2 = { fileData, fileSize, 0 };
            png_set_read_fn(png, &r2, _png_mem_read);
            png_read_info(png, info);

            bit_depth = png_get_bit_depth(png, info);
            color_type = png_get_color_type(png, info);
            if (bit_depth == 16) png_set_strip_16(png);
            if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
            if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
            if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
            if (color_type == PNG_COLOR_TYPE_RGB ||
                color_type == PNG_COLOR_TYPE_GRAY ||
                color_type == PNG_COLOR_TYPE_PALETTE)
                png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
            if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
                png_set_gray_to_rgb(png);

            png_read_update_info(png, info);

            pal_count = 0;
            memset(palette, 0, 512 * sizeof(g2dColor));
            bool still_overflow = false;

            row = (unsigned char*)malloc(rowbytes);
            if (!row) { png_destroy_read_struct(&png, &info, NULL); break; }

            for (png_uint_32 y = 0; y < height && !still_overflow; y++) {
                png_read_row(png, row, NULL);
                g2dColor* pixels = (g2dColor*)row;

                for (png_uint_32 x = 0; x < width; x++) {
                    g2dColor c = pixels[x];
                    if (loadtype == TEX_WHITE) {
                        c = G2D_RGBA(255, 255, 255, G2D_GET_A(c));
                    } else if (loadtype == TEX_GRAYSCALE) {
                        Uint8 r = G2D_GET_R(c) * 0.299;
                        Uint8 g = G2D_GET_G(c) * 0.587;
                        Uint8 b = G2D_GET_B(c) * 0.114;
                        const double gray = floor(r + g + b + 0.5);
                        c = G2D_RGBA(gray, gray, gray, G2D_GET_A(c));
                    }
                    g2dColor q = G2D_RGBA(
                        G2D_GET_R(c) & mask,
                        G2D_GET_G(c) & mask,
                        G2D_GET_B(c) & mask,
                        G2D_GET_A(c) & mask);

                    int found = -1;
                    for (int j = 0; j < pal_count; j++) {
                        if (palette[j] == q) { found = j; break; }
                    }
                    if (found >= 0) continue;
                    if (pal_count < max_colors) {
                        palette[pal_count++] = q;
                    } else {
                        still_overflow = true;
                        break;
                    }
                }
            }

            free(row);
            png_destroy_read_struct(&png, &info, NULL);

            if (!still_overflow) break;
        }
    }

    if (pal_count == 0) {
        free(palette);
        return NULL;
    }

    int cols = (width + GR_MAX_TEX_SIZE - 1) / GR_MAX_TEX_SIZE;
    int rows = (height + GR_MAX_TEX_SIZE - 1) / GR_MAX_TEX_SIZE;
    int tile_count = cols * rows;

    g2dImage* tiled = (g2dImage*)calloc(1, sizeof(g2dImage));
    if (!tiled) { free(palette); return NULL; }

    tiled->tiled = true;
    tiled->cols = cols;
    tiled->rows = rows;
    tiled->w = width;
    tiled->h = height;
    tiled->tw = width;
    tiled->th = height;
    tiled->ratio = (float)width / (float)height;
    tiled->can_blend = true;
    tiled->format = hw_format;
    tiled->swizzled = false;
    tiled->data = NULL;
    tiled->palette = palette;

    tiled->tiles = (g2dImage**)malloc(tile_count * sizeof(g2dImage*));
    if (!tiled->tiles) { free(tiled); free(palette); return NULL; }
    memset(tiled->tiles, 0, tile_count * sizeof(g2dImage*));

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, _png_error_dummy, _png_warn_dummy);
    if (!png) { /* cleanup */ }
    info = png_create_info_struct(png);
    PNGMemReader reader2 = { fileData, fileSize, 0 };
    png_set_read_fn(png, &reader2, _png_mem_read);
    png_read_info(png, info);

    bit_depth = png_get_bit_depth(png, info);
    color_type = png_get_color_type(png, info);
    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY || color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);

    png_read_update_info(png, info);

    int quant_mask = 0xFF;
    if (overflow) {
        for (int sh = 1; sh <= 4; sh++) {
            int test_mask = 0xFF & ~((1 << sh) - 1);
            bool all_quantized = true;
            for (int j = 0; j < pal_count; j++) {
                g2dColor p = palette[j];
                if ((G2D_GET_R(p) & ~test_mask) ||
                    (G2D_GET_G(p) & ~test_mask) ||
                    (G2D_GET_B(p) & ~test_mask) ||
                    (G2D_GET_A(p) & ~test_mask)) {
                    all_quantized = false;
                    break;
                }
            }
            if (all_quantized) { quant_mask = test_mask; break; }
        }
    }

    row = (unsigned char*)malloc(rowbytes);
    if (!row) {
        png_destroy_read_struct(&png, &info, NULL);
        // cleanup
        for (int i = 0; i < tile_count; i++) if (tiled->tiles[i]) g2dTexFree(&tiled->tiles[i]);
        free(tiled->tiles);
        free(palette);
        free(tiled);
        return NULL;
    }

    typedef struct {
        unsigned char* idx;
        int tw, th;
        int w, h;
        int y_written;
        int x0, y0;
        int ti;
    } TileBuild;

    TileBuild* tb = (TileBuild*)calloc(tile_count, sizeof(TileBuild));
    if (!tb) {
        free(row);
        png_destroy_read_struct(&png, &info, NULL);
        // cleanup
        for (int i = 0; i < tile_count; i++) if (tiled->tiles[i]) g2dTexFree(&tiled->tiles[i]);
        free(tiled->tiles);
        free(palette);
        free(tiled);
        return NULL;
    }

    bool alloc_failed = false;
    for (int r = 0; r < rows; r++) {
        for (int c = 0; c < cols; c++) {
            int ti = r * cols + c;
            int x0 = c * GR_MAX_TEX_SIZE;
            int y0 = r * GR_MAX_TEX_SIZE;
            int tw = width - x0;  if (tw > GR_MAX_TEX_SIZE) tw = GR_MAX_TEX_SIZE;
            int th = height - y0; if (th > GR_MAX_TEX_SIZE) th = GR_MAX_TEX_SIZE;

            // power-of-2
            int tw2 = 1; while (tw2 < tw) tw2 <<= 1;
            int th2 = 1; while (th2 < th) th2 <<= 1;

            tb[ti].tw = tw2;
            tb[ti].th = th2;
            tb[ti].w = tw;
            tb[ti].h = th;
            tb[ti].x0 = x0;
            tb[ti].y0 = y0;
            tb[ti].ti = ti;
            tb[ti].y_written = 0;

            size_t idx_size;
            if (hw_format == GU_PSM_T8) idx_size = (size_t)tw2 * th2;
            else                        idx_size = (size_t)tw2 * th2 / 2;

            tb[ti].idx = (unsigned char*)calloc(1, idx_size);
            if (!tb[ti].idx) { alloc_failed = true; break; }
        }
        if (alloc_failed) break;
    }

    if (alloc_failed) {
        for (int i = 0; i < tile_count; i++) if (tb[i].idx) free(tb[i].idx);
        free(tb);
        free(row);
        png_destroy_read_struct(&png, &info, NULL);
        for (int i = 0; i < tile_count; i++) if (tiled->tiles[i]) g2dTexFree(&tiled->tiles[i]);
        free(tiled->tiles);
        free(palette);
        free(tiled);
        return NULL;
    }

    for (png_uint_32 y = 0; y < height; y++) {
        png_read_row(png, row, NULL);
        g2dColor* pixels = (g2dColor*)row;

        int r = y / GR_MAX_TEX_SIZE;

        for (int c = 0; c < cols; c++) {
            int ti = r * cols + c;
            TileBuild* t = &tb[ti];
            if (t->y_written >= t->h) continue;

            int x_start = t->x0;
            int x_end = x_start + t->w;
            if (x_end > (int)width) x_end = width;

            int local_y = (int)y - t->y0;
            if (local_y < 0 || local_y >= t->h) continue;

            if (hw_format == GU_PSM_T8) {
                unsigned char* dst = t->idx + (size_t)local_y * t->tw;

                for (int x = x_start; x < x_end; x++) {
                    g2dColor col = pixels[x];
                    if (loadtype == TEX_WHITE) {
                        col = G2D_RGBA(255, 255, 255, G2D_GET_A(col));
                    } else if (loadtype == TEX_GRAYSCALE) {
                        Uint8 rr = G2D_GET_R(col) * 0.299;
                        Uint8 gg = G2D_GET_G(col) * 0.587;
                        Uint8 bb = G2D_GET_B(col) * 0.114;
                        const double gray = floor(rr + gg + bb + 0.5);
                        col = G2D_RGBA(gray, gray, gray, G2D_GET_A(col));
                    }
                    if (overflow) {
                        col = G2D_RGBA(
                            G2D_GET_R(col) & quant_mask,
                            G2D_GET_G(col) & quant_mask,
                            G2D_GET_B(col) & quant_mask,
                            G2D_GET_A(col) & quant_mask);
                    }
                    dst[x - x_start] = (unsigned char)_g2dPaletteLookup(col, palette, pal_count, 256);
                }
            } else {
                // T4
                unsigned char* dst = t->idx;

                for (int x = x_start; x < x_end; x++) {
                    g2dColor col = pixels[x];
                    if (loadtype == TEX_WHITE) {
                        col = G2D_RGBA(255, 255, 255, G2D_GET_A(col));
                    } else if (loadtype == TEX_GRAYSCALE) {
                        Uint8 rr = G2D_GET_R(col) * 0.299;
                        Uint8 gg = G2D_GET_G(col) * 0.587;
                        Uint8 bb = G2D_GET_B(col) * 0.114;
                        const double gray = floor(rr + gg + bb + 0.5);
                        col = G2D_RGBA(gray, gray, gray, G2D_GET_A(col));
                    }
                    if (overflow) {
                        col = G2D_RGBA(
                            G2D_GET_R(col) & quant_mask,
                            G2D_GET_G(col) & quant_mask,
                            G2D_GET_B(col) & quant_mask,
                            G2D_GET_A(col) & quant_mask);
                    }
                    int local_x = x - x_start;
                    int i = local_y * t->tw + local_x;
                    int p = _g2dPaletteLookup(col, palette, pal_count, 16) & 0x0F;
                    if ((i & 1) == 0) dst[i >> 1] |= p;
                    else              dst[i >> 1] |= (p << 4);
                }
            }
        }

        for (int c = 0; c < cols; c++) {
            int ti = r * cols + c;
            if (tb[ti].y_written < tb[ti].h) tb[ti].y_written++;
        }
    }

    free(row);
    png_destroy_read_struct(&png, &info, NULL);

    bool tile_failed = false;
    for (int i = 0; i < tile_count; i++) {
        TileBuild* t = &tb[i];

        g2dImage* tile = (g2dImage*)calloc(1, sizeof(g2dImage));
        if (!tile) { tile_failed = true; break; }

        tile->w = t->w;
        tile->h = t->h;
        tile->tw = t->tw;
        tile->th = t->th;
        tile->ratio = (float)t->w / (float)t->h;
        tile->can_blend = true;
        tile->tiled = false;
        tile->cols = tile->rows = 1;
        tile->format = hw_format;
        tile->palette = NULL;
        tile->data = t->idx;
        tile->swizzled = false;

        if (tile->tw >= 16) {
            _g2dSwizzle(tile);
        }

        tiled->tiles[i] = tile;
    }

    for (int i = 0; i < tile_count; i++) {
        if (tile_failed && tb[i].idx) free(tb[i].idx);
    }
    free(tb);

    if (tile_failed) {
        for (int i = 0; i < tile_count; i++) if (tiled->tiles[i]) g2dTexFree(&tiled->tiles[i]);
        free(tiled->tiles);
        free(palette);
        free(tiled);
        return NULL;
    }

    sceKernelDcacheWritebackAll();
    return tiled;
}

g2dImage* LoadImage(const char* filename, const TextureLoadType loadtype, g2dTexFormat format, uint32_t (*collision_surface)[16] /*= NULL*/)
{
    unsigned char* fileIn = NULL;
    size_t length = 0;
    FILESYSTEM_loadAssetToMemory(filename, &fileIn, &length);
    if (fileIn == NULL)
    {
        assert(0 && "Image file missing!");
        return NULL;
    }

    unsigned int width = 0, height = 0;

    {
        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, _png_error_dummy, _png_warn_dummy);
        if (!png) { VVV_free(fileIn); return NULL; }
        png_infop info = png_create_info_struct(png);
        if (!info) { png_destroy_read_struct(&png, NULL, NULL); VVV_free(fileIn); return NULL; }
        PNGMemReader r = { fileIn, length, 0 };
        png_set_read_fn(png, &r, _png_mem_read);
        png_read_info(png, info);
        width = png_get_image_width(png, info);
        height = png_get_image_height(png, info);
        png_destroy_read_struct(&png, &info, NULL);
    }

    if (width == 0 || height == 0) {
        VVV_free(fileIn);
        return NULL;
    }

    if (loadtype == TEX_WHITE) {
        format = G2D_CLUT4;
    }

    int hw_format = GU_PSM_8888;
    if (format == G2D_CLUT8)      hw_format = GU_PSM_T8;
    else if (format == G2D_CLUT4) hw_format = GU_PSM_T4;

    if (width > 512 || height > 512)
    {
        g2dImage* tiled = _G2DLoadTiledFromPNG(fileIn, length, filename, loadtype, hw_format);
        VVV_free(fileIn);

        if (!tiled) {
            vlog_error("Failed to tile image: %s", filename);
            return NULL;
        }
        return tiled;
    }

    unsigned char* rgbaData = NULL;
    unsigned int error = lodepng_decode32(&rgbaData, &width, &height, fileIn, length);
    VVV_free(fileIn);

    if (error != 0)
    {
        vlog_error("Could not load %s: %s", filename, lodepng_error_text(error));
        return NULL;
    }

    g2dImage* tempTex = _g2dTexCreate(width, height, true);
    if (tempTex == NULL)
    {
        free(rgbaData);
        return NULL;
    }

    int bytesPerPixel = 4;
    int srcRowSize = width * bytesPerPixel;
    int dstRowSize = tempTex->tw * bytesPerPixel;

    for (unsigned int y = 0; y < height; y++)
    {
        memcpy((char*) tempTex->data + y * dstRowSize, rgbaData + y * srcRowSize, srcRowSize);
    }

    if (collision_surface != NULL) {
        for (int y = 0; y < 512; y++)
        {
            for (int x = 0; x < 512; x++)
            {
                if (G2D_GET_A(get_pixel(tempTex, x, y)) != 0)
                {
                    sprites_collision_surface_set_bit(collision_surface, x, y);
                }
                else
                {
                    sprites_collision_surface_clear_bit(collision_surface, x, y);
                }
            }
        }
    }

    // Apply Format
    int curwidth = tempTex->w;
    int curheight = tempTex->h;

    switch (loadtype)
    {
    case TEX_WHITE:
        for (int y = 0; y < curheight; y++)
        {
            for (int x = 0; x < curwidth; x++)
            {
                g2dColor color = get_pixel(tempTex, x, y);
                set_pixel(tempTex, x, y, G2D_RGBA(255, 255, 255, G2D_GET_A(color)));
            }
        }
        break;
    case TEX_GRAYSCALE:
        for (int y = 0; y < curheight; y++)
        {
            for (int x = 0; x < curwidth; x++)
            {
                g2dColor color = get_pixel(tempTex, x, y);

                // Magic numbers used for grayscaling (eyes perceive certain colors brighter than others)
                Uint8 r = G2D_GET_R(color) * 0.299;
                Uint8 g = G2D_GET_G(color) * 0.587;
                Uint8 b = G2D_GET_B(color) * 0.114;

                const double gray = floor(r + g + b + 0.5);

                set_pixel(tempTex, x, y, G2D_RGBA(gray, gray, gray, G2D_GET_A(color)));
            }
        }
        break;
    default:
        break;
    }

    // Pallete Apply
    g2dImage* resultTex = NULL;

    if (loadtype == TEX_WHITE)
    {
        format = G2D_CLUT4;
    }

    if (format == G2D_RGBA8888)
    {
        resultTex = tempTex;
    }
    else
    {
        resultTex = (g2dImage*)calloc(1, sizeof(g2dImage));
        if (resultTex == NULL)
        {
            g2dTexFree(&tempTex);
            free(rgbaData);
            return NULL;
        }

        resultTex->w = tempTex->w;
        resultTex->h = tempTex->h;
        resultTex->tw = tempTex->tw;
        resultTex->th = tempTex->th;
        resultTex->ratio = tempTex->ratio;
        resultTex->can_blend = tempTex->can_blend;
        resultTex->swizzled = false;

        int hw_format = (format == G2D_CLUT8) ? GU_PSM_T8 : GU_PSM_T4;
        _g2dApplyFormat(resultTex, (g2dColor*) tempTex->data, hw_format);

        tempTex->data = NULL;
        g2dTexFree(&tempTex);

        free(rgbaData);
    }

    _g2dSwizzle(resultTex);

    sceKernelDcacheWritebackAll();

    return resultTex;
}

g2dImage* LoadImage(const char* filename, g2dTexFormat format)
{
    return LoadImage(filename, TEX_COLOR, format);
}

/* Any unneeded variants can be NULL */
static void LoadVariants(const char* filename, g2dTexFormat format, g2dImage** colored, g2dImage** white, g2dImage** grayscale)
{
    if (colored != NULL) *colored = LoadImage(filename, TEX_COLOR, format);
    if (white != NULL) *white = LoadImage(filename, TEX_WHITE, G2D_CLUT4);
    if (grayscale != NULL) *grayscale = LoadImage(filename, TEX_GRAYSCALE, format);
}

static void LoadSpritesTranslation(
    const char* filename,
    tinyxml2::XMLDocument* mask,
    const char* english_sprites_filename,
    g2dImage** texture
) {
    /* Create a sprites texture for display in another language.
     * texture_english is used as a base. Parts of the translation (filename)
     * will replace parts of the base, as instructed in the mask XML. */

    // Load original English sprites, for working with
    g2dImage* original = NULL;
    {
        unsigned char* fileIn = NULL;
        size_t length = 0;
        FILESYSTEM_loadAssetToMemory(english_sprites_filename, &fileIn, &length);
        if (fileIn == NULL)
        {
            assert(0 && "Image file missing!");
            return;
        }

        unsigned int width = 0, height = 0;

        {
            png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, _png_error_dummy, _png_warn_dummy);
            if (!png) { VVV_free(fileIn); return; }
            png_infop info = png_create_info_struct(png);
            if (!info) { png_destroy_read_struct(&png, NULL, NULL); VVV_free(fileIn); return; }
            PNGMemReader r = { fileIn, length, 0 };
            png_set_read_fn(png, &r, _png_mem_read);
            png_read_info(png, info);
            width = png_get_image_width(png, info);
            height = png_get_image_height(png, info);
            png_destroy_read_struct(&png, &info, NULL);
        }

        if (width == 0 || height == 0) {
            VVV_free(fileIn);
            return;
        }

        unsigned char* rgbaData = NULL;
        unsigned int error = lodepng_decode32(&rgbaData, &width, &height, fileIn, length);
        VVV_free(fileIn);

        if (error != 0)
        {
            vlog_error("Could not load %s: %s", english_sprites_filename, lodepng_error_text(error));
            return;
        }

        original = _g2dTexCreate(width, height, true);
        if (original == NULL)
        {
            free(rgbaData);
            return;
        }

        int bytesPerPixel = 4;
        int srcRowSize = width * bytesPerPixel;
        int dstRowSize = (original)->tw * bytesPerPixel;

        for (unsigned int y = 0; y < height; y++)
        {
            memcpy((char*) original->data + y * dstRowSize, rgbaData + y * srcRowSize, srcRowSize);
        }
        free(rgbaData);

        int curwidth = original->w;
        int curheight = original->h;

        for (int y = 0; y < curheight; y++)
        {
            for (int x = 0; x < curwidth; x++)
            {
                g2dColor color = get_pixel(original, x, y);
                set_pixel(original, x, y, G2D_RGBA(255, 255, 255, G2D_GET_A(color)));
            }
        }
    }

    g2dImage* translated = NULL;
    {
        unsigned char* fileIn = NULL;
        size_t length = 0;
        FILESYSTEM_loadAssetToMemory(filename, &fileIn, &length);
        if (fileIn == NULL)
        {
            assert(0 && "Image file missing!");
            return;
        }

        unsigned int width = 0, height = 0;

        {
            png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, _png_error_dummy, _png_warn_dummy);
            if (!png) { VVV_free(fileIn); return; }
            png_infop info = png_create_info_struct(png);
            if (!info) { png_destroy_read_struct(&png, NULL, NULL); VVV_free(fileIn); return; }
            PNGMemReader r = { fileIn, length, 0 };
            png_set_read_fn(png, &r, _png_mem_read);
            png_read_info(png, info);
            width = png_get_image_width(png, info);
            height = png_get_image_height(png, info);
            png_destroy_read_struct(&png, &info, NULL);
        }

        if (width == 0 || height == 0) {
            VVV_free(fileIn);
            return;
        }

        unsigned char* rgbaData = NULL;
        unsigned int error = lodepng_decode32(&rgbaData, &width, &height, fileIn, length);
        VVV_free(fileIn);

        if (error != 0)
        {
            vlog_error("Could not load %s: %s", filename, lodepng_error_text(error));
            return;
        }

        translated = _g2dTexCreate(width, height, true);
        if (translated == NULL)
        {
            free(rgbaData);
            return;
        }

        int bytesPerPixel = 4;
        int srcRowSize = width * bytesPerPixel;
        int dstRowSize = translated->tw * bytesPerPixel;

        for (unsigned int y = 0; y < height; y++)
        {
            memcpy((char*) translated->data + y * dstRowSize, rgbaData + y * srcRowSize, srcRowSize);
        }
        free(rgbaData);

        int curwidth = translated->w;
        int curheight = translated->h;

        for (int y = 0; y < curheight; y++)
        {
            for (int x = 0; x < curwidth; x++)
            {
                g2dColor color = get_pixel(translated, x, y);
                set_pixel(translated, x, y, G2D_RGBA(255, 255, 255, G2D_GET_A(color)));
            }
        }
    }

    tinyxml2::XMLHandle hMask(mask);
    tinyxml2::XMLElement* pElem;

    int sprite_w = 1, sprite_h = 1;
    if ((pElem = mask->FirstChildElement()) != NULL)
    {
        sprite_w = pElem->IntAttribute("sprite_w", 1);
        sprite_h = pElem->IntAttribute("sprite_h", 1);
    }

    FOR_EACH_XML_ELEMENT(hMask, pElem)
    {
        EXPECT_ELEM(pElem, "sprite");

        int x = pElem->IntAttribute("x", 0);
        int y = pElem->IntAttribute("y", 0);
        VVV_Rect src;
        src.x = x * sprite_w;
        src.y = y * sprite_h;
        src.w = pElem->IntAttribute("w", 1) * sprite_w;
        src.h = pElem->IntAttribute("h", 1) * sprite_h;

        VVV_Rect dst;
        dst.x = pElem->IntAttribute("dx", x) * sprite_w;
        dst.y = pElem->IntAttribute("dy", y) * sprite_h;

        {
            g2dColor* wdata = (g2dColor*)original->data;
            g2dColor* tdata = (g2dColor*)translated->data;

            for (int j = 0; j < src.h; j++) {
                g2dColor* wrow = wdata + (dst.y + j) * original->tw + dst.x;
                g2dColor* trow = tdata + (src.y + j) * translated->tw + src.x;

                for (int i = 0; i < src.w; i++) {
                    Uint8 a = G2D_GET_A(trow[i]);
                    wrow[i] = G2D_RGBA(255, 255, 255, a);
                }
            }
        }
    }

    *texture = (g2dImage*)calloc(1, sizeof(g2dImage));
    if (*texture == NULL)
    {
        g2dTexFree(texture);
        return;
    }

    (*texture)->w = translated->w;
    (*texture)->h = translated->h;
    (*texture)->tw = translated->tw;
    (*texture)->th = translated->th;
    (*texture)->ratio = translated->ratio;
    (*texture)->can_blend = translated->can_blend;
    (*texture)->swizzled = false;

    _g2dApplyFormat(*texture, (g2dColor*) original->data, GU_PSM_T4);

    translated->data = NULL;
    g2dTexFree(&translated);

    _g2dSwizzle(*texture);

    sceKernelDcacheWritebackAll();
}

void GraphicsResources::init_translations(void)
{
    if (im_sprites_translated) g2dTexFree(&im_sprites_translated);

    if (loc::english_sprites)
    {
        return;
    }

    const char* langcode = loc::lang.c_str();

    const char* path_template = "lang/%s/graphics/%s";
    char path_xml[256];
    char path_sprites[256];
    char path_flipsprites[256];
    snprintf(path_xml, sizeof(path_xml), path_template, langcode, "spritesmask.xml");
    snprintf(path_sprites, sizeof(path_sprites), path_template, langcode, "sprites.png");
    snprintf(path_flipsprites, sizeof(path_flipsprites), path_template, langcode, "flipsprites.png");

    /* We don't want to apply main-game translations to level-specific (custom) sprites.
     * Either sprites and translations are BOTH main-game, or BOTH level-specific.
     * Our pivots are the XML (it _has_ to exist for translated sprites to work) and
     * graphics/sprites.png (what sense does it make to have only flipsprites). */
    if (FILESYSTEM_isAssetMounted(path_xml) != FILESYSTEM_isAssetMounted("graphics/sprites.png"))
    {
        return;
    }

    tinyxml2::XMLDocument doc_mask;
    if (!FILESYSTEM_loadAssetTiXml2Document(path_xml, doc_mask))
    {
        // Only try to load the images if the XML document exists
        return;
    }

    if (FILESYSTEM_areAssetsInSameRealDir(path_xml, path_sprites))
    {
        LoadSpritesTranslation(
            path_sprites,
            &doc_mask,
            "graphics/sprites.png",
            &im_sprites_translated
        );
    }
}

void GraphicsResources::init(void)
{

    LoadVariants("graphics/tiles.png", G2D_CLUT8, &im_tiles, &im_tiles_white, &im_tiles_tint);
    LoadVariants("graphics/tiles2.png", G2D_CLUT8, &im_tiles2, NULL, &im_tiles2_tint);
    LoadVariants("graphics/entcolours.png", G2D_CLUT8, &im_entcolours, NULL, &im_entcolours_tint);

    im_sprites = LoadImage("graphics/sprites.png", TEX_WHITE, G2D_CLUT4, sprites_collision_surface_normal);
    im_flipsprites = LoadImage("graphics/flipsprites.png", TEX_WHITE, G2D_CLUT4, sprites_collision_surface_flipped);
    if (im_flipsprites) g2dTexFree(&im_flipsprites);

    im_tiles3 = LoadImage("graphics/tiles3.png", G2D_CLUT8);
    im_teleporter = LoadImage("graphics/teleporter.png", TEX_WHITE, G2D_CLUT4);

    im_image0 = LoadImage("graphics/levelcomplete.png", G2D_CLUT4);
    im_image1 = LoadImage("graphics/minimap.png", G2D_CLUT8);
    im_image2 = LoadImage("graphics/covered.png", G2D_CLUT8);
    im_image3 = LoadImage("graphics/elephant.png", TEX_WHITE, G2D_CLUT4);
    im_image4 = LoadImage("graphics/gamecomplete.png", G2D_CLUT4);
    im_image5 = LoadImage("graphics/fliplevelcomplete.png", G2D_CLUT4);
    im_image6 = LoadImage("graphics/flipgamecomplete.png", G2D_CLUT4);
    im_image7 = LoadImage("graphics/site.png", TEX_WHITE, G2D_CLUT4);
    im_image8 = LoadImage("graphics/site2.png", TEX_WHITE, G2D_CLUT4);
    im_image9 = LoadImage("graphics/site3.png", TEX_WHITE, G2D_CLUT4);
    im_image10 = LoadImage("graphics/ending.png", G2D_CLUT4);
    im_image11 = LoadImage("graphics/site4.png", TEX_WHITE, G2D_CLUT4);

    im_sprites_translated = NULL;

    init_translations();

    im_image12 = _g2dTexCreate(240, 180, false);
}


void GraphicsResources::destroy(void)
{
#define CLEAR(img) if (img) g2dTexFree(&img)
    CLEAR(im_tiles);
    CLEAR(im_tiles_white);
    CLEAR(im_tiles_tint);
    CLEAR(im_tiles2);
    CLEAR(im_tiles2_tint);
    CLEAR(im_tiles3);
    CLEAR(im_entcolours);
    CLEAR(im_entcolours_tint);
    CLEAR(im_sprites);
    CLEAR(im_flipsprites);
    CLEAR(im_teleporter);

    CLEAR(im_image0);
    CLEAR(im_image1);
    CLEAR(im_image2);
    CLEAR(im_image3);
    CLEAR(im_image4);
    CLEAR(im_image5);
    CLEAR(im_image6);
    CLEAR(im_image7);
    CLEAR(im_image8);
    CLEAR(im_image9);
    CLEAR(im_image10);
    CLEAR(im_image11);
    CLEAR(im_image12);

    CLEAR(im_sprites_translated);
#undef CLEAR
}

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

static void _g2dApplyFormat(g2dImage *tex, g2dColor *rgba_buffer, int target_hw_format) {
    int total_pixels = tex->tw * tex->th;
    tex->format = target_hw_format;

    if (tex->data) {
        free(tex->data);
        tex->data = NULL;
    }

    if (target_hw_format == GU_PSM_8888) {
        tex->data = malloc(total_pixels * 4);
        if (!tex->data) {
            free(rgba_buffer);
            return;
        }
        memcpy(tex->data, rgba_buffer, total_pixels * 4);
        tex->palette = NULL;
        free(rgba_buffer);
    }
    else {
        tex->palette = (g2dColor *)memalign(16, 512 * sizeof(g2dColor));
        if (!tex->palette) {
            free(rgba_buffer);
            return;
        }
        memset(tex->palette, 0, 512 * sizeof(g2dColor));

        int pal_count = 0;
        if (target_hw_format == GU_PSM_T8) {
            unsigned char *indices = (unsigned char *)malloc(total_pixels);
            if (!indices) {
                free(tex->palette);
                tex->palette = NULL;
                free(rgba_buffer);
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

        free(rgba_buffer);
    }

    sceKernelDcacheWritebackAll();
}

static void _g2dSwizzle(g2dImage *tex) {
    int width_in_bytes = 0;
    if (tex->format == GU_PSM_8888) {
        width_in_bytes = tex->tw * 4;
    } else if (tex->format == GU_PSM_T8) {
        width_in_bytes = tex->tw;
    } else if (tex->format == GU_PSM_T4) {
        width_in_bytes = tex->tw / 2;
    } else {
        return;
    }

    if (width_in_bytes < 16) return;

    unsigned char *tmp = (unsigned char *)malloc(width_in_bytes * tex->th);
    if (!tmp) return;

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

static int _g2dPaletteLookup(g2dColor c, g2dColor *palette, int count,
                             int max_colors) {
    for (int j = 0; j < count; j++) {
        if (palette[j] == c) return j;
    }
    for (int shift = 1; shift <= 4; shift++) {
        int mask = 0xFF & ~((1 << shift) - 1);
        g2dColor q = G2D_RGBA(
            G2D_GET_R(c) & mask,
            G2D_GET_G(c) & mask,
            G2D_GET_B(c) & mask,
            G2D_GET_A(c) & mask);
        for (int j = 0; j < count; j++) {
            if (palette[j] == q) return j;
        }
    }
    return 0;
}