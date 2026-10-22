#include "GraphicsResources.h"

#include <pspkernel.h>
#include <pspgu.h>
#include <malloc.h>
#include <cassert>
#include <cmath>

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

static int _getNextPower2(int n)
{
    int p = 1;
    while ((p <<= 1) < n);
    return p;
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

static inline g2dColor _apply_loadtype(g2dColor c, TextureLoadType loadtype)
{
    if (loadtype == TEX_WHITE)
    {
        return G2D_RGBA(255, 255, 255, G2D_GET_A(c));
    }
    else if (loadtype == TEX_GRAYSCALE)
    {
        uint8_t r = G2D_GET_R(c) * 0.299;
        uint8_t g = G2D_GET_G(c) * 0.587;
        uint8_t b = G2D_GET_B(c) * 0.114;
        const double gray = floor(r + g + b + 0.5);
        return G2D_RGBA(gray, gray, gray, G2D_GET_A(c));
    }
    return c;
}

static png_structp _png_open_rgba(const unsigned char* data, size_t size, PNGMemReader* reader)
{
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, _png_error_dummy, _png_warn_dummy);
    if (!png) return NULL;
    png_infop info = png_create_info_struct(png);
    if (!info)
    {
        png_destroy_read_struct(&png, NULL, NULL);
        return NULL;
    }

    reader->data = data;
    reader->size = size;
    reader->pos  = 0;
    png_set_read_fn(png, reader, _png_mem_read);
    png_read_info(png, info);

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
    return png;
}

g2dImage* LoadImageInto(
    g2dImage* tex,
    const char* filename,
    TextureLoadType loadtype,
    uint32_t (*collision_surface)[16]
) {
    if (tex == NULL) return NULL;

    unsigned char* fileIn = NULL;
    size_t length = 0;
    FILESYSTEM_loadAssetToMemory(filename, &fileIn, &length);
    if (fileIn == NULL)
    {
        assert(0 && "Image file missing!");
        return NULL;
    }

    const int width  = tex->w;
    const int height = tex->h;
    const int hw_format = tex->format;

    const bool is_clut = (hw_format == GU_PSM_T8 || hw_format == GU_PSM_T4);
    const int max_colors = (hw_format == GU_PSM_T8) ? 256
                         : (hw_format == GU_PSM_T4) ? 16
                         : 0;

    unsigned char* rgbaData = NULL;
    unsigned int w = 0, h = 0;
    unsigned int error = lodepng_decode32(&rgbaData, &w, &h, fileIn, length);
    VVV_free(fileIn);

    if (error != 0)
    {
        vlog_error("Could not load %s: %s", filename, lodepng_error_text(error));
        return NULL;
    }

    int w_use = (int) w;
    int h_use = (int) h;
    if (w_use != width || h_use != height)
    {
        vlog_warn("LoadImageInto: size mismatch for %s (tex %dx%d, png %dx%d)", filename, width, height, w_use, h_use);
        w_use = (width  < w_use) ? width  : w_use;
        h_use = (height < h_use) ? height : h_use;
    }

    if (collision_surface != NULL)
    {
        memset(collision_surface, 0, 512 * 16 * sizeof(uint32_t));
        for (int y = 0; y < h_use; y++)
        {
            for (int x = 0; x < w_use; x++)
            {
                g2dColor raw_c = 0;
                if (x < w_use && y < h_use)
                {
                    raw_c = ((const g2dColor*) rgbaData)[y * w_use + x];
                }
                if (G2D_GET_R(raw_c) != 0 && G2D_GET_A(raw_c) != 0)
                    sprites_collision_surface_set_bit(collision_surface, x, y);
                else
                    sprites_collision_surface_clear_bit(collision_surface, x, y);
            }
        }
    }

    PalMap pal_map;
    int pal_count = 0;
    bool overflow = false;
    int quant_mask = 0xFF;

    if (is_clut)
    {
        _pal_init(&pal_map, max_colors);

        for (int y = 0; y < h_use; y++)
        {
            const g2dColor* pixels = (const g2dColor*)rgbaData + y * w_use;
            for (int x = 0; x < w_use; x++)
            {
                g2dColor c = _apply_loadtype(pixels[x], loadtype);
                if (_pal_find_or_add(&pal_map, c) < 0)
                {
                    overflow = true;
                    goto pass1_done;
                }
            }
        }

    pass1_done:
        pal_count = pal_map.count;

        if (overflow)
        {
            for (int shift = 1; shift <= 4; shift++)
            {
                int mask = 0xFF & ~((1 << shift) - 1);
                _pal_init(&pal_map, max_colors);
                bool still_overflow = false;

                for (int y = 0; y < h_use && !still_overflow; y++)
                {
                    const g2dColor* pixels = (const g2dColor*) rgbaData + y * w_use;
                    for (int x = 0; x < w_use; x++) {
                        g2dColor c = _apply_loadtype(pixels[x], loadtype);
                        g2dColor q = G2D_RGBA(
                            G2D_GET_R(c) & mask,
                            G2D_GET_G(c) & mask,
                            G2D_GET_B(c) & mask,
                            G2D_GET_A(c) & mask);
                        if (_pal_find_or_add(&pal_map, q) < 0)
                        {
                            still_overflow = true;
                            break;
                        }
                    }
                }

                if (!still_overflow)
                {
                    pal_count = pal_map.count;
                    quant_mask = mask;
                    break;
                }
            }
        }

        if (pal_count == 0)
        {
            free(rgbaData);
            return NULL;
        }

        memset(tex->palette, 0, 512 * sizeof(g2dColor));
        int copy_n = pal_count;
        if (copy_n > max_colors) copy_n = max_colors;
        memcpy(tex->palette, pal_map.palette, copy_n * sizeof(g2dColor));

        _pal_init(&pal_map, max_colors);
        for (int i = 0; i < copy_n; i++)
        {
            _pal_find_or_add(&pal_map, tex->palette[i]);
        }
    }

    for (int y = 0; y < h_use; y++)
    {
        const g2dColor* pixels = (const g2dColor*) rgbaData + y * w_use;

        int local_y0 = 0;
        int tile_cols = 1;
        if (tex->tiled)
        {
            int r = y / GR_MAX_TEX_SIZE;
            local_y0 = r * GR_MAX_TEX_SIZE;
            tile_cols = tex->cols;
        }

        for (int x = 0; x < w_use; x++)
        {
            g2dColor raw = pixels[x];
            g2dColor c = _apply_loadtype(raw, loadtype);

            g2dImage* dst_tex;
            int local_x0;
            if (!tex->tiled)
            {
                dst_tex = tex;
                local_x0 = 0;
            }
            else
            {
                int c_idx = x / GR_MAX_TEX_SIZE;
                local_x0 = c_idx * GR_MAX_TEX_SIZE;
                dst_tex = tex->tiles[(y / GR_MAX_TEX_SIZE) * tile_cols + c_idx];
            }

            int lx = x - local_x0;
            int ly = y - local_y0;

            if (hw_format == GU_PSM_8888)
            {
                ((g2dColor*) dst_tex->data)[ly * dst_tex->tw + lx] = c;
            }
            else if (hw_format == GU_PSM_T8)
            {
                if (overflow)
                {
                    c = G2D_RGBA(
                        G2D_GET_R(c) & quant_mask,
                        G2D_GET_G(c) & quant_mask,
                        G2D_GET_B(c) & quant_mask,
                        G2D_GET_A(c) & quant_mask);
                }
                int idx = _g2dPaletteLookup(c, &pal_map, 256);
                ((u8*)dst_tex->data)[ly * dst_tex->tw + lx] = (u8)idx;
            }
            else if (hw_format == GU_PSM_T4)
            {
                if (overflow)
                {
                    c = G2D_RGBA(
                        G2D_GET_R(c) & quant_mask,
                        G2D_GET_G(c) & quant_mask,
                        G2D_GET_B(c) & quant_mask,
                        G2D_GET_A(c) & quant_mask);
                }
                int idx = _g2dPaletteLookup(c, &pal_map, 16) & 0x0F;
                int i = ly * dst_tex->tw + lx;
                u8* data = (u8*)dst_tex->data;
                if ((i & 1) == 0) data[i >> 1] |= idx;
                else data[i >> 1] |= (idx << 4);
            }
        }
    }

    free(rgbaData);

    if (!tex->tiled)
    {
        if (tex->tw >= 16) _g2dSwizzle(tex);
    }
    else
    {
        for (int i = 0; i < tex->cols * tex->rows; i++)
        {
            g2dImage* tile = tex->tiles[i];
            if (tile && tile->tw >= 16) _g2dSwizzle(tile);
        }
    }

    sceKernelDcacheWritebackAll();
    return tex;
}

static g2dImage* ReserveImageMemory(const char* filename, g2dTexFormat format)
{
    unsigned char* fileIn = NULL;
    size_t length = 0;
    FILESYSTEM_loadAssetToMemory(filename, &fileIn, &length);
    if (fileIn == NULL)
    {
        return NULL;
    }

    unsigned int width = 0, height = 0;
    {
        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, _png_error_dummy, _png_warn_dummy);
        if (!png)
        {
            VVV_free(fileIn);
            return NULL;
        }
        png_infop info = png_create_info_struct(png);
        if (!info)
        {
            png_destroy_read_struct(&png, NULL, NULL);
            VVV_free(fileIn);
            return NULL;
        }
        PNGMemReader r = { fileIn, length, 0 };
        png_set_read_fn(png, &r, _png_mem_read);
        png_read_info(png, info);
        width  = png_get_image_width(png, info);
        height = png_get_image_height(png, info);
        png_destroy_read_struct(&png, &info, NULL);
    }

    VVV_free(fileIn);

    if (width == 0 || height == 0)
    {
        return NULL;
    }

    g2dImage* tex = (g2dImage*) calloc(1, sizeof(g2dImage));
    if (tex == NULL) return NULL;

    tex->w = width;
    tex->h = height;
    tex->tw = _getNextPower2(width);
    tex->th = _getNextPower2(height);
    tex->ratio = (float) width / (float) height;
    tex->can_blend = true;
    tex->swizzled = false;
    tex->palette = NULL;
    tex->data = NULL;

    int hw_format = GU_PSM_8888;
    switch (format)
    {
        case G2D_RGBA8888: hw_format = GU_PSM_8888; break;
        case G2D_CLUT8:    hw_format = GU_PSM_T8;   break;
        case G2D_CLUT4:    hw_format = GU_PSM_T4;   break;
        default: free(tex); return NULL;
    }
    tex->format = hw_format;

    if (width <= GR_MAX_TEX_SIZE && height <= GR_MAX_TEX_SIZE)
    {
        tex->tiled = false;
        tex->cols = tex->rows = 1;

        if (hw_format == GU_PSM_8888)
        {
            tex->data = malloc((size_t) tex->tw * tex->th * 4);
            if (!tex->data)
            {
                free(tex);
                return NULL;
            }
            memset(tex->data, 0, (size_t) tex->tw * tex->th * 4);
        }
        else
        {
            tex->palette = (g2dColor*) memalign(16, 512 * sizeof(g2dColor));
            if (!tex->palette)
            {
                free(tex);
                return NULL;
            }
            memset(tex->palette, 0, 512 * sizeof(g2dColor));

            size_t data_size;
            if (hw_format == GU_PSM_T8) data_size = (size_t) tex->tw * tex->th;
            else data_size = (size_t) tex->tw * tex->th / 2;

            tex->data = malloc(data_size);
            if (!tex->data)
            {
                free(tex->palette);
                free(tex);
                return NULL;
            }
            memset(tex->data, 0, data_size);
        }

        return tex;
    }

    int cols = (width  + GR_MAX_TEX_SIZE - 1) / GR_MAX_TEX_SIZE;
    int rows = (height + GR_MAX_TEX_SIZE - 1) / GR_MAX_TEX_SIZE;
    int tile_count = cols * rows;

    tex->tiled = true;
    tex->cols = cols;
    tex->rows = rows;

    if (hw_format == GU_PSM_T8 || hw_format == GU_PSM_T4)
    {
        tex->palette = (g2dColor*) memalign(16, 512 * sizeof(g2dColor));
        if (!tex->palette)
        {
            free(tex);
            return NULL;
        }
        memset(tex->palette, 0, 512 * sizeof(g2dColor));
    }

    tex->tiles = (g2dImage**) malloc(tile_count * sizeof(g2dImage*));
    if (!tex->tiles)
    {
        if (tex->palette) free(tex->palette);
        free(tex);
        return NULL;
    }
    memset(tex->tiles, 0, tile_count * sizeof(g2dImage*));

    for (int r = 0; r < rows; r++)
    {
        for (int c = 0; c < cols; c++)
        {
            int ti = r * cols + c;
            int tw = width  - c * GR_MAX_TEX_SIZE; if (tw > GR_MAX_TEX_SIZE) tw = GR_MAX_TEX_SIZE;
            int th = height - r * GR_MAX_TEX_SIZE; if (th > GR_MAX_TEX_SIZE) th = GR_MAX_TEX_SIZE;

            int tw2 = _getNextPower2(tw);
            int th2 = _getNextPower2(th);

            g2dImage* tile = (g2dImage*) calloc(1, sizeof(g2dImage));
            if (!tile)
            {
                for (int k = 0; k < ti; k++)
                {
                    if (tex->tiles[k]) g2dTexFree(&tex->tiles[k]);
                }
                free(tex->tiles);
                if (tex->palette) free(tex->palette);
                free(tex);
                return NULL;
            }

            tile->w = tw;
            tile->h = th;
            tile->tw = tw2;
            tile->th = th2;
            tile->ratio = (float) tw / (float) th;
            tile->can_blend = true;
            tile->tiled = false;
            tile->cols = tile->rows = 1;
            tile->format = hw_format;
            tile->palette = NULL;
            tile->swizzled = false;

            if (hw_format == GU_PSM_8888)
            {
                tile->data = malloc((size_t) tw2 * th2 * 4);
                if (!tile->data)
                {
                    free(tile);
                    return NULL;
                }
                memset(tile->data, 0, (size_t) tw2 * th2 * 4);
            }
            else if (hw_format == GU_PSM_T8)
            {
                tile->data = malloc((size_t) tw2 * th2);
                if (!tile->data)
                {
                    free(tile);
                    return NULL;
                }
                memset(tile->data, 0, (size_t) tw2 * th2);
            }
            else
            {
                tile->data = malloc((size_t) tw2 * th2 / 2);
                if (!tile->data)
                {
                    free(tile);
                    return NULL;
                }
                memset(tile->data, 0, (size_t) tw2 * th2 / 2);
            }

            tex->tiles[ti] = tile;
        }
    }

    return tex;
}

g2dImage* LoadImage(const char* filename, const TextureLoadType loadtype, g2dTexFormat format, uint32_t (*collision_surface)[16])
{
    g2dImage* tex = ReserveImageMemory(filename, format);
    if (tex == NULL) return NULL;

    LoadImageInto(tex, filename, loadtype, collision_surface);

    return tex;
}

void LoadVariantsInto(
    const char* filename,
    g2dImage* colored,
    g2dImage* white,
    g2dImage* grayscale
) {
    if (colored   != NULL) LoadImageInto(colored,   filename, TEX_COLOR, NULL);
    if (white     != NULL) LoadImageInto(white,     filename, TEX_WHITE, NULL);
    if (grayscale != NULL) LoadImageInto(grayscale, filename, TEX_GRAYSCALE, NULL);
}

static void LoadSpritesTranslationInto(
    g2dImage* tex,
    const char* filename,
    tinyxml2::XMLDocument* mask,
    const char* english_sprites_filename
) {
    if (tex == NULL) return;

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
            width  = png_get_image_width(png, info);
            height = png_get_image_height(png, info);
            png_destroy_read_struct(&png, &info, NULL);
        }

        if (width == 0 || height == 0){ VVV_free(fileIn); return; }

        unsigned char* rgbaData = NULL;
        unsigned int error = lodepng_decode32(&rgbaData, &width, &height, fileIn, length);
        VVV_free(fileIn);

        if (error != 0)
        {
            vlog_error("Could not load %s: %s", english_sprites_filename, lodepng_error_text(error));
            return;
        }

        original = _g2dTexCreate(width, height, true);
        if (original == NULL) { free(rgbaData); return; }

        int bytesPerPixel = 4;
        int srcRowSize = width * bytesPerPixel;
        int dstRowSize = original->tw * bytesPerPixel;
        for (unsigned int y = 0; y < height; y++)
        {
            memcpy((char*)original->data + y * dstRowSize, rgbaData + y * srcRowSize, srcRowSize);
        }
        free(rgbaData);

        for (int y = 0; y < original->h; y++) {
            for (int x = 0; x < original->w; x++) {
                g2dColor c = get_pixel(original, x, y);
                set_pixel(original, x, y, G2D_RGBA(255, 255, 255, G2D_GET_A(c)));
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
            g2dTexFree(&original);
            return;
        }

        unsigned int width = 0, height = 0;
        {
            png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, _png_error_dummy, _png_warn_dummy);
            if (!png) { VVV_free(fileIn); g2dTexFree(&original); return; }
            png_infop info = png_create_info_struct(png);
            if (!info) { png_destroy_read_struct(&png, NULL, NULL); VVV_free(fileIn); g2dTexFree(&original); return; }
            PNGMemReader r = { fileIn, length, 0 };
            png_set_read_fn(png, &r, _png_mem_read);
            png_read_info(png, info);
            width  = png_get_image_width(png, info);
            height = png_get_image_height(png, info);
            png_destroy_read_struct(&png, &info, NULL);
        }

        if (width == 0 || height == 0) { VVV_free(fileIn); g2dTexFree(&original); return; }

        unsigned char* rgbaData = NULL;
        unsigned int error = lodepng_decode32(&rgbaData, &width, &height, fileIn, length);
        VVV_free(fileIn);

        if (error != 0)
        {
            vlog_error("Could not load %s: %s", filename, lodepng_error_text(error));
            g2dTexFree(&original);
            return;
        }

        translated = _g2dTexCreate(width, height, true);
        if (translated == NULL) { free(rgbaData); g2dTexFree(&original); return; }

        int bytesPerPixel = 4;
        int srcRowSize = width * bytesPerPixel;
        int dstRowSize = translated->tw * bytesPerPixel;
        for (unsigned int y = 0; y < height; y++)
        {
            memcpy((char*)translated->data + y * dstRowSize, rgbaData + y * srcRowSize, srcRowSize);
        }
        free(rgbaData);

        for (int y = 0; y < translated->h; y++)
        {
            for (int x = 0; x < translated->w; x++)
            {
                g2dColor c = get_pixel(translated, x, y);
                set_pixel(translated, x, y, G2D_RGBA(255, 255, 255, G2D_GET_A(c)));
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

        g2dColor* wdata = (g2dColor*) original->data;
        g2dColor* tdata = (g2dColor*) translated->data;
        for (int j = 0; j < src.h; j++)
        {
            g2dColor* wrow = wdata + (dst.y + j) * original->tw + dst.x;
            g2dColor* trow = tdata + (src.y + j) * translated->tw + src.x;
            for (int i = 0; i < src.w; i++)
            {
                uint8_t a = G2D_GET_A(trow[i]);
                wrow[i] = G2D_RGBA(255, 255, 255, a);
            }
        }
    }

    {
        int max_colors = 16;

        PalMap pal_map;
        _pal_init(&pal_map, max_colors);

        bool overflow = false;
        for (int y = 0; y < tex->h; y++)
        {
            for (int x = 0; x < tex->w; x++)
            {
                g2dColor c = get_pixel(original, x, y);
                if (_pal_find_or_add(&pal_map, c) < 0)
                {
                    overflow = true;
                    break;
                }
            }
            if (overflow) break;
        }

        int quant_mask = 0xFF;
        if (overflow)
        {
            for (int shift = 1; shift <= 4; shift++)
            {
                int mask = 0xFF & ~((1 << shift) - 1);
                _pal_init(&pal_map, max_colors);
                overflow = false;
                for (int y = 0; y < tex->h; y++)
                {
                    for (int x = 0; x < tex->w; x++)
                    {
                        g2dColor c = get_pixel(original, x, y);
                        g2dColor q = G2D_RGBA(
                            G2D_GET_R(c) & mask,
                            G2D_GET_G(c) & mask,
                            G2D_GET_B(c) & mask,
                            G2D_GET_A(c) & mask);
                        if (_pal_find_or_add(&pal_map, q) < 0)
                        {
                            overflow = true;
                            break;
                        }
                    }
                    if (overflow) break;
                }
                if (!overflow)
                {
                    quant_mask = mask;
                    break;
                }
            }
        }

        memset(tex->palette, 0, 512 * sizeof(g2dColor));
        int copy_n = pal_map.count;
        if (copy_n > max_colors) copy_n = max_colors;
        memcpy(tex->palette, pal_map.palette, copy_n * sizeof(g2dColor));

        _pal_init(&pal_map, max_colors);
        for (int i = 0; i < max_colors; i++)
        {
            _pal_find_or_add(&pal_map, tex->palette[i]);
        }

        memset(tex->data, 0, (size_t) tex->tw * tex->th / 2);

        for (int y = 0; y < tex->h; y++)
        {
            for (int x = 0; x < tex->w; x++)
            {
                g2dColor c = get_pixel(original, x, y);
                if (overflow)
                {
                    c = G2D_RGBA(
                        G2D_GET_R(c) & quant_mask,
                        G2D_GET_G(c) & quant_mask,
                        G2D_GET_B(c) & quant_mask,
                        G2D_GET_A(c) & quant_mask);
                }
                int idx = _g2dPaletteLookup(c, &pal_map, 16) & 0x0F;
                int i = y * tex->tw + x;
                u8* data = (u8*)tex->data;
                if ((i & 1) == 0) data[i >> 1] |= idx;
                else data[i >> 1] |= (idx << 4);
            }
        }
    }

    g2dTexFree(&original);
    g2dTexFree(&translated);

    if (tex->tw >= 16) _g2dSwizzle(tex);

    sceKernelDcacheWritebackAll();
}


g2dImage* LoadFont(const char* filename, const TextureLoadType loadtype)
{
    unsigned char* fileData = NULL;
    size_t fileSize = 0;
    FILESYSTEM_loadAssetToMemory(filename, &fileData, &fileSize);
    if (fileData == NULL)
    {
        assert(0 && "Font PNG missing!");
        return NULL;
    }

    unsigned int width = 0, height = 0;
    {
        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, _png_error_dummy, _png_warn_dummy);
        if (!png) { VVV_free(fileData); return NULL; }
        png_infop info = png_create_info_struct(png);
        if (!info) { png_destroy_read_struct(&png, NULL, NULL); VVV_free(fileData); return NULL; }

        PNGMemReader r = { fileData, fileSize, 0 };
        png_set_read_fn(png, &r, _png_mem_read);
        png_read_info(png, info);
        width  = png_get_image_width(png, info);
        height = png_get_image_height(png, info);
        png_destroy_read_struct(&png, &info, NULL);
    }

    if (width == 0 || height == 0)
    {
        VVV_free(fileData);
        return NULL;
    }

    const int hw_format = GU_PSM_T4;
    const int max_colors = 16;

    g2dImage* tex = (g2dImage*) calloc(1, sizeof(g2dImage));
    if (tex == NULL) { VVV_free(fileData); return NULL; }

    tex->w = width;
    tex->h = height;
    tex->tw = _getNextPower2(width);
    tex->th = _getNextPower2(height);
    tex->ratio = (float)width / (float)height;
    tex->can_blend = true;
    tex->swizzled = false;
    tex->palette = NULL;
    tex->data = NULL;
    tex->format = hw_format;

    const bool tiled = (width > GR_MAX_TEX_SIZE || height > GR_MAX_TEX_SIZE);

    if (!tiled)
    {
        tex->tiled = false;
        tex->cols = tex->rows = 1;

        tex->palette = (g2dColor*) memalign(16, 512 * sizeof(g2dColor));
        if (!tex->palette) { free(tex); VVV_free(fileData); return NULL; }
        memset(tex->palette, 0, 512 * sizeof(g2dColor));

        tex->data = malloc((size_t) tex->tw * tex->th / 2);
        if (!tex->data) { free(tex->palette); free(tex); VVV_free(fileData); return NULL; }
        memset(tex->data, 0, (size_t) tex->tw * tex->th / 2);
    }
    else
    {
        int cols = (width  + GR_MAX_TEX_SIZE - 1) / GR_MAX_TEX_SIZE;
        int rows = (height + GR_MAX_TEX_SIZE - 1) / GR_MAX_TEX_SIZE;
        int tile_count = cols * rows;

        tex->tiled = true;
        tex->cols = cols;
        tex->rows = rows;

        tex->palette = (g2dColor*) memalign(16, 512 * sizeof(g2dColor));
        if (!tex->palette) { free(tex); VVV_free(fileData); return NULL; }
        memset(tex->palette, 0, 512 * sizeof(g2dColor));

        tex->tiles = (g2dImage**)malloc(tile_count * sizeof(g2dImage*));
        if (!tex->tiles)
        {
            free(tex->palette); free(tex); VVV_free(fileData); return NULL;
        }
        memset(tex->tiles, 0, tile_count * sizeof(g2dImage*));

        for (int r = 0; r < rows; r++)
        {
            for (int c = 0; c < cols; c++)
            {
                int ti = r * cols + c;
                int tw = width  - c * GR_MAX_TEX_SIZE; if (tw > GR_MAX_TEX_SIZE) tw = GR_MAX_TEX_SIZE;
                int th = height - r * GR_MAX_TEX_SIZE; if (th > GR_MAX_TEX_SIZE) th = GR_MAX_TEX_SIZE;

                int tw2 = _getNextPower2(tw);
                int th2 = _getNextPower2(th);

                g2dImage* tile = (g2dImage*) calloc(1, sizeof(g2dImage));
                if (!tile) {
                    for (int k = 0; k < ti; k++)
                    {
                        if (tex->tiles[k]) g2dTexFree(&tex->tiles[k]);
                    }
                    free(tex->tiles);
                    free(tex->palette);
                    free(tex);
                    VVV_free(fileData);
                    return NULL;
                }

                tile->w = tw;
                tile->h = th;
                tile->tw = tw2;
                tile->th = th2;
                tile->ratio = (float)tw / (float)th;
                tile->can_blend = true;
                tile->tiled = false;
                tile->cols = tile->rows = 1;
                tile->format = hw_format;
                tile->palette = NULL;
                tile->swizzled = false;
                tile->data = malloc((size_t)tw2 * th2 / 2);
                if (!tile->data)
                {
                    free(tile);
                    for (int k = 0; k < ti; k++)
                    {
                        if (tex->tiles[k]) g2dTexFree(&tex->tiles[k]);
                    }
                    free(tex->tiles);
                    free(tex->palette);
                    free(tex);
                    VVV_free(fileData);
                    return NULL;
                }
                memset(tile->data, 0, (size_t) tw2 * th2 / 2);

                tex->tiles[ti] = tile;
            }
        }
    }

    const size_t rowbytes = (size_t) width * 4;
    unsigned char* row = (unsigned char*) malloc(rowbytes);
    if (!row)
    {
        g2dTexFree(&tex);
        VVV_free(fileData);
        return NULL;
    }

    PalMap pal_map;
    _pal_init(&pal_map, max_colors);
    bool overflow = false;
    int quant_mask = 0xFF;

    if (loadtype == TEX_WHITE)
    {
        tex->palette[0] = G2D_RGBA(0, 0, 0, 0);
        tex->palette[1] = G2D_RGBA(255, 255, 255, 255);
        _pal_find_or_add(&pal_map, tex->palette[0]);
        _pal_find_or_add(&pal_map, tex->palette[1]);

        PNGMemReader reader;
        png_structp png = _png_open_rgba(fileData, fileSize, &reader);
        if (!png) { free(row); g2dTexFree(&tex); VVV_free(fileData); return NULL; }

        for (unsigned int y = 0; y < height; y++)
        {
            png_read_row(png, row, NULL);
            const g2dColor* pixels = (const g2dColor*) row;

            int local_y0 = 0;
            int tile_cols = 1;
            if (tiled)
            {
                int r = y / GR_MAX_TEX_SIZE;
                local_y0 = r * GR_MAX_TEX_SIZE;
                tile_cols = tex->cols;
            }

            for (unsigned int x = 0; x < width; x++)
            {
                uint8_t a = G2D_GET_A(pixels[x]);

                g2dImage* dst_tex;
                int local_x0;
                if (!tiled)
                {
                    dst_tex = tex;
                    local_x0 = 0;
                }
                else
                {
                    int c_idx = x / GR_MAX_TEX_SIZE;
                    local_x0 = c_idx * GR_MAX_TEX_SIZE;
                    dst_tex = tex->tiles[(y / GR_MAX_TEX_SIZE) * tile_cols + c_idx];
                    if (dst_tex == NULL) continue;
                }

                int lx = x - local_x0;
                int ly = y - local_y0;

                int idx = (a == 0) ? 0 : 1;
                int i = ly * dst_tex->tw + lx;
                u8* data = (u8*)dst_tex->data;
                if ((i & 1) == 0) data[i >> 1] |= idx;
                else data[i >> 1] |= (idx << 4);
            }
        }

        png_destroy_read_struct(&png, NULL, NULL);
    }
    else
    {
        {
            PNGMemReader reader;
            png_structp png = _png_open_rgba(fileData, fileSize, &reader);
            if (!png) { free(row); g2dTexFree(&tex); VVV_free(fileData); return NULL; }

            for (unsigned int y = 0; y < height; y++)
            {
                png_read_row(png, row, NULL);
                const g2dColor* pixels = (const g2dColor*) row;
                for (unsigned int x = 0; x < width; x++)
                {
                    g2dColor c = _apply_loadtype(pixels[x], loadtype);
                    if (_pal_find_or_add(&pal_map, c) < 0)
                    {
                        overflow = true;
                        goto pass1_done;
                    }
                }
            }

        pass1_done:
            png_destroy_read_struct(&png, NULL, NULL);
        }

        if (overflow)
        {
            for (int shift = 1; shift <= 4; shift++)
            {
                int mask = 0xFF & ~((1 << shift) - 1);

                PNGMemReader reader;
                png_structp png = _png_open_rgba(fileData, fileSize, &reader);
                if (!png) { free(row); g2dTexFree(&tex); VVV_free(fileData); return NULL; }

                _pal_init(&pal_map, max_colors);
                bool still_overflow = false;

                for (unsigned int y = 0; y < height && !still_overflow; y++)
                {
                    png_read_row(png, row, NULL);
                    const g2dColor* pixels = (const g2dColor*) row;
                    for (unsigned int x = 0; x < width; x++)
                    {
                        g2dColor c = _apply_loadtype(pixels[x], loadtype);
                        g2dColor q = G2D_RGBA(
                            G2D_GET_R(c) & mask,
                            G2D_GET_G(c) & mask,
                            G2D_GET_B(c) & mask,
                            G2D_GET_A(c) & mask);
                        if (_pal_find_or_add(&pal_map, q) < 0)
                        {
                            still_overflow = true;
                            break;
                        }
                    }
                }
                png_destroy_read_struct(&png, NULL, NULL);

                if (!still_overflow)
                {
                    quant_mask = mask;
                    break;
                }
            }
        }

        memset(tex->palette, 0, 512 * sizeof(g2dColor));
        int copy_n = pal_map.count;
        if (copy_n > max_colors) copy_n = max_colors;
        memcpy(tex->palette, pal_map.palette, copy_n * sizeof(g2dColor));

        _pal_init(&pal_map, max_colors);
        for (int i = 0; i < max_colors; i++)
        {
            _pal_find_or_add(&pal_map, tex->palette[i]);
        }

        {
            PNGMemReader reader;
            png_structp png = _png_open_rgba(fileData, fileSize, &reader);
            if (!png) { free(row); g2dTexFree(&tex); VVV_free(fileData); return NULL; }

            for (unsigned int y = 0; y < height; y++)
            {
                png_read_row(png, row, NULL);
                g2dColor* pixels = (g2dColor*) row;

                int local_y0 = 0;
                int tile_cols = 1;
                if (tiled)
                {
                    int r = y / GR_MAX_TEX_SIZE;
                    local_y0 = r * GR_MAX_TEX_SIZE;
                    tile_cols = tex->cols;
                }

                for (unsigned int x = 0; x < width; x++)
                {
                    g2dColor c = _apply_loadtype(pixels[x], loadtype);

                    g2dImage* dst_tex;
                    int local_x0;
                    if (!tiled)
                    {
                        dst_tex = tex;
                        local_x0 = 0;
                    }
                    else
                    {
                        int c_idx = x / GR_MAX_TEX_SIZE;
                        local_x0 = c_idx * GR_MAX_TEX_SIZE;
                        dst_tex = tex->tiles[(y / GR_MAX_TEX_SIZE) * tile_cols + c_idx];
                        if (dst_tex == NULL) continue;
                    }

                    int lx = x - local_x0;
                    int ly = y - local_y0;

                    if (overflow)
                    {
                        c = G2D_RGBA(
                            G2D_GET_R(c) & quant_mask,
                            G2D_GET_G(c) & quant_mask,
                            G2D_GET_B(c) & quant_mask,
                            G2D_GET_A(c) & quant_mask);
                    }
                    int idx = _g2dPaletteLookup(c, &pal_map, 16) & 0x0F;
                    int i = ly * dst_tex->tw + lx;
                    u8* data = (u8*) dst_tex->data;
                    if ((i & 1) == 0) data[i >> 1] |= idx;
                    else data[i >> 1] |= (idx << 4);
                }
            }

            png_destroy_read_struct(&png, NULL, NULL);
        }
    }

    free(row);
    VVV_free(fileData);

    if (!tiled)
    {
        if (tex->tw >= 16) _g2dSwizzle(tex);
    }
    else
    {
        for (int i = 0; i < tex->cols * tex->rows; i++)
        {
            g2dImage* tile = tex->tiles[i];
            if (tile && tile->tw >= 16) _g2dSwizzle(tile);
        }
    }

    sceKernelDcacheWritebackAll();
    return tex;
}

void GraphicsResources::init_translations(void)
{
    sprites_translated_valid = false;
    flipsprites_translated_valid = false;

    if (loc::english_sprites)
    {
        return;
    }

    if (im_sprites_translated == NULL && im_flipsprites_translated == NULL)
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
        LoadSpritesTranslationInto(
            im_sprites_translated,
            path_sprites,
            &doc_mask,
            "graphics/sprites.png"
        );
        if (im_sprites_translated != NULL)
        {
            sprites_translated_valid = true;
        }
    }
    if (FILESYSTEM_areAssetsInSameRealDir(path_xml, path_flipsprites))
    {
        LoadSpritesTranslationInto(
            im_flipsprites_translated,
            path_flipsprites,
            &doc_mask,
            "graphics/flipsprites.png"
        );
        if (im_flipsprites_translated != NULL)
        {
            flipsprites_translated_valid = true;
        }
    }
}

void GraphicsResources::preserve(void) {
    im_tiles = ReserveImageMemory("graphics/tiles.png", G2D_CLUT8);
    im_tiles_white = ReserveImageMemory("graphics/tiles.png", G2D_CLUT4);
    im_tiles_tint = ReserveImageMemory("graphics/tiles.png", G2D_CLUT8);

    im_tiles2 = ReserveImageMemory("graphics/tiles2.png", G2D_CLUT8);
    im_tiles2_tint = ReserveImageMemory("graphics/tiles2.png", G2D_CLUT8);

    im_entcolours = ReserveImageMemory("graphics/entcolours.png", G2D_CLUT8);
    im_entcolours_tint = ReserveImageMemory("graphics/entcolours.png", G2D_CLUT8);

    im_sprites = ReserveImageMemory("graphics/sprites.png", G2D_CLUT4);
    im_flipsprites = ReserveImageMemory("graphics/flipsprites.png", G2D_CLUT4);

    im_tiles3 = ReserveImageMemory("graphics/tiles3.png", G2D_CLUT8);
    im_teleporter = ReserveImageMemory("graphics/teleporter.png", G2D_CLUT4);

    im_image1 = ReserveImageMemory("graphics/minimap.png", G2D_CLUT8);
    im_image2 = ReserveImageMemory("graphics/covered.png", G2D_CLUT8);
    im_image3 = ReserveImageMemory("graphics/elephant.png", G2D_CLUT4);
    im_image7 = ReserveImageMemory("graphics/site.png", G2D_CLUT4);
    im_image8 = ReserveImageMemory("graphics/site2.png", G2D_CLUT4);
    im_image9 = ReserveImageMemory("graphics/site3.png", G2D_CLUT4);
    im_image10 = ReserveImageMemory("graphics/ending.png", G2D_CLUT4);

    im_image12 = (g2dImage*) calloc(1, sizeof(g2dImage));
    if (im_image12)
    {
        im_image12->w = 240;
        im_image12->h = 180;
        im_image12->tw = 256;
        im_image12->th = 256;
        im_image12->ratio = 240.0f / 180.0f;
        im_image12->can_blend = false;
        im_image12->swizzled = false;
        im_image12->tiled = false;
        im_image12->cols = im_image12->rows = 1;
        im_image12->format = GU_PSM_T4;

        im_image12->palette = (g2dColor*) memalign(16, 512 * sizeof(g2dColor));
        im_image12->data = malloc((size_t) im_image12->tw * im_image12->th / 2);

        if (!im_image12->palette || !im_image12->data)
        {
            if (im_image12->palette) free(im_image12->palette);
            if (im_image12->data) free(im_image12->data);
            free(im_image12);
            im_image12 = NULL;
        }
        else
        {
            memset(im_image12->palette, 0, 512 * sizeof(g2dColor));
            im_image12->palette[0] = G2D_RGBA(0, 0, 0, 255);
            im_image12->palette[1] = G2D_RGB(96, 96, 96);
            im_image12->palette[2] = G2D_RGB(196, 196, 196);

            memset(im_image12->data, 0, (size_t) im_image12->tw * im_image12->th / 2);
        }

        sceKernelDcacheWritebackAll();
    }

    im_sprites_translated = ReserveImageMemory("graphics/sprites.png", G2D_CLUT4);
    im_flipsprites_translated = ReserveImageMemory("graphics/flipsprites.png", G2D_CLUT4);

    sprites_translated_valid = false;
    flipsprites_translated_valid = false;
}

static void g2dImageZero(g2dImage* tex)
{
    if (tex == NULL) return;

    if (tex->tiled && tex->tiles != NULL)
    {
        for (int i = 0; i < tex->cols * tex->rows; i++)
        {
            g2dImage* tile = tex->tiles[i];
            if (tile == NULL) continue;

            if (tile->data != NULL) 
            {
                size_t size;
                if (tile->format == GU_PSM_T8) size = (size_t)tile->tw * tile->th;
                else if (tile->format == GU_PSM_T4) size = (size_t)tile->tw * tile->th / 2;
                else size = (size_t)tile->tw * tile->th * 4;
                memset(tile->data, 0, size);
            }
            tile->swizzled = false;
        }
        if (tex->palette != NULL)
        {
            memset(tex->palette, 0, 512 * sizeof(g2dColor));
        }
    }
    else
    {
        if (tex->data != NULL)
        {
            size_t size;
            if (tex->format == GU_PSM_T8) size = (size_t) tex->tw * tex->th;
            else if (tex->format == GU_PSM_T4) size = (size_t) tex->tw * tex->th / 2;
            else size = (size_t) tex->tw * tex->th * 4;
            memset(tex->data, 0, size);
        }
        if (tex->palette != NULL)
        {
            memset(tex->palette, 0, 512 * sizeof(g2dColor));
        }
        tex->swizzled = false;
    }
}

static bool graphics_loaded = false;

static bool im_tiles_is_custom = false;
static bool im_tiles2_is_custom = false;
static bool im_entcolours_is_custom = false;
static bool im_sprites_is_custom = false;
static bool im_flipsprites_is_custom = false;
static bool im_tiles3_is_custom = false;
static bool im_teleporter_is_custom = false;
static bool im_image1_is_custom = false;
static bool im_image2_is_custom = false;
static bool im_image3_is_custom = false;
static bool im_image7_is_custom = false;
static bool im_image8_is_custom = false;
static bool im_image9_is_custom = false;
static bool im_image10_is_custom = false;

void GraphicsResources::init(void)
{
    if (!graphics_loaded || im_tiles_is_custom != FILESYSTEM_customAssetExists("graphics/tiles.png"))
    {
        im_tiles_is_custom = FILESYSTEM_customAssetExists("graphics/tiles.png");
        g2dImageZero(im_tiles);
        g2dImageZero(im_tiles_white);
        g2dImageZero(im_tiles_tint);
        LoadVariantsInto("graphics/tiles.png", im_tiles, im_tiles_white, im_tiles_tint);
    }

    if (!graphics_loaded || im_tiles2_is_custom != FILESYSTEM_customAssetExists("graphics/tiles2.png"))
    {
        im_tiles2_is_custom = FILESYSTEM_customAssetExists("graphics/tiles2.png");
        g2dImageZero(im_tiles2);
        g2dImageZero(im_tiles2_tint);
        LoadVariantsInto("graphics/tiles2.png", im_tiles2, NULL, im_tiles2_tint);
    }

    if (!graphics_loaded || im_entcolours_is_custom != FILESYSTEM_customAssetExists("graphics/entcolours.png"))
    {
        im_entcolours_is_custom = FILESYSTEM_customAssetExists("graphics/entcolours.png");
        g2dImageZero(im_entcolours);
        g2dImageZero(im_entcolours_tint);
        LoadVariantsInto("graphics/entcolours.png", im_entcolours, NULL, im_entcolours_tint);
    }

    if (!graphics_loaded || im_sprites_is_custom != FILESYSTEM_customAssetExists("graphics/sprites.png"))
    {
        im_sprites_is_custom = FILESYSTEM_customAssetExists("graphics/sprites.png");
        g2dImageZero(im_sprites);
        LoadImageInto(im_sprites, "graphics/sprites.png", TEX_WHITE, sprites_collision_surface_normal);
    }

    if (!graphics_loaded || im_flipsprites_is_custom != FILESYSTEM_customAssetExists("graphics/flipsprites.png"))
    {
        im_flipsprites_is_custom = FILESYSTEM_customAssetExists("graphics/flipsprites.png");
        g2dImageZero(im_flipsprites);
        LoadImageInto(im_flipsprites, "graphics/flipsprites.png", TEX_WHITE, sprites_collision_surface_flipped);
    }

    if (!graphics_loaded || im_tiles3_is_custom != FILESYSTEM_customAssetExists("graphics/tiles3.png"))
    {
        im_tiles3_is_custom = FILESYSTEM_customAssetExists("graphics/tiles3.png");
        g2dImageZero(im_tiles3);
        LoadImageInto(im_tiles3, "graphics/tiles3.png", TEX_COLOR, NULL);
    }

    if (!graphics_loaded || im_teleporter_is_custom != FILESYSTEM_customAssetExists("graphics/teleporter.png"))
    {
        im_teleporter_is_custom = FILESYSTEM_customAssetExists("graphics/teleporter.png");
        g2dImageZero(im_teleporter);
        LoadImageInto(im_teleporter, "graphics/teleporter.png", TEX_WHITE, NULL);
    }

    if (!graphics_loaded || im_image1_is_custom != FILESYSTEM_customAssetExists("graphics/minimap.png"))
    {
        im_image1_is_custom = FILESYSTEM_customAssetExists("graphics/minimap.png");
        g2dImageZero(im_image1);
        LoadImageInto(im_image1, "graphics/minimap.png", TEX_COLOR, NULL);
    }

    if (!graphics_loaded || im_image2_is_custom != FILESYSTEM_customAssetExists("graphics/covered.png"))
    {
        im_image2_is_custom = FILESYSTEM_customAssetExists("graphics/covered.png");
        g2dImageZero(im_image2);
        LoadImageInto(im_image2, "graphics/covered.png", TEX_COLOR, NULL);
    }

    if (!graphics_loaded || im_image3_is_custom != FILESYSTEM_customAssetExists("graphics/elephant.png"))
    {
        im_image3_is_custom = FILESYSTEM_customAssetExists("graphics/elephant.png");
        g2dImageZero(im_image3);
        LoadImageInto(im_image3, "graphics/elephant.png", TEX_WHITE, NULL);
    }

    if (!graphics_loaded || im_image7_is_custom != FILESYSTEM_customAssetExists("graphics/site.png"))
    {
        im_image7_is_custom = FILESYSTEM_customAssetExists("graphics/site.png");
        g2dImageZero(im_image7);
        LoadImageInto(im_image7, "graphics/site.png", TEX_WHITE, NULL);
    }

    if (!graphics_loaded || im_image8_is_custom != FILESYSTEM_customAssetExists("graphics/site2.png"))
    {
        im_image8_is_custom = FILESYSTEM_customAssetExists("graphics/site2.png");
        g2dImageZero(im_image8);
        LoadImageInto(im_image8, "graphics/site2.png", TEX_WHITE, NULL);
    }

    if (!graphics_loaded || im_image9_is_custom != FILESYSTEM_customAssetExists("graphics/site3.png"))
    {
        im_image9_is_custom = FILESYSTEM_customAssetExists("graphics/site3.png");
        g2dImageZero(im_image9);
        LoadImageInto(im_image9, "graphics/site3.png", TEX_WHITE, NULL);
    }

    if (!graphics_loaded || im_image10_is_custom != FILESYSTEM_customAssetExists("graphics/ending.png"))
    {
        im_image10_is_custom = FILESYSTEM_customAssetExists("graphics/ending.png");
        g2dImageZero(im_image10);
        LoadImageInto(im_image10, "graphics/ending.png", TEX_COLOR, NULL);
    }

    init_translations();

    graphics_loaded = true;
}

void GraphicsResources::destroy(void)
{
    g2dImageZero(im_flipsprites_translated);
    g2dImageZero(im_sprites_translated);

    g2dImageZero(im_image10);
    g2dImageZero(im_image9);
    g2dImageZero(im_image8);
    g2dImageZero(im_image7);
    g2dImageZero(im_image3);
    g2dImageZero(im_image2);
    g2dImageZero(im_image1);

    g2dImageZero(im_teleporter);
    g2dImageZero(im_flipsprites);
    g2dImageZero(im_sprites);
    g2dImageZero(im_entcolours_tint);
    g2dImageZero(im_entcolours);
    g2dImageZero(im_tiles3);
    g2dImageZero(im_tiles2_tint);
    g2dImageZero(im_tiles2);
    g2dImageZero(im_tiles_tint);
    g2dImageZero(im_tiles_white);
    g2dImageZero(im_tiles);

    sprites_translated_valid = false;
    flipsprites_translated_valid = false;

    graphics_loaded = false;
}