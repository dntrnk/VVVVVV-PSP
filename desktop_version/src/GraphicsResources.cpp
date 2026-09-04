#include "GraphicsResources.h"

#include <pspkernel.h>
#include <pspgu.h>
#include <malloc.h>

#include <time.h>
#include <tinyxml2.h>

#include "Alloc.h"
#include "FileSystemUtils.h"
#include "Graphics.h"
#include "GraphicsUtil.h"
#include "Localization.h"
#include "Vlogging.h"
#include "Screen.h"
#include "XMLUtils.h"

// Copy+Paste from glib2d.c
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
    extern unsigned lodepng_encode24(
        unsigned char** out,
        size_t* outsize,
        const unsigned char* image,
        unsigned w,
        unsigned h
    );
    extern const char* lodepng_error_text(unsigned code);
}

static SDL_Surface* LoadImageRaw(const char* filename, unsigned char** data)
{
    *data = NULL;

    // Temporary storage for the image that's loaded
    SDL_Surface* loadedImage = NULL;

    unsigned int width, height;
    unsigned int error;

    unsigned char* fileIn;
    size_t length;
    FILESYSTEM_loadAssetToMemory(filename, &fileIn, &length);
    if (fileIn == NULL)
    {
        SDL_assert(0 && "Image file missing!");
        return NULL;
    }
    error = lodepng_decode32(data, &width, &height, fileIn, length);
    VVV_free(fileIn);

    if (error != 0)
    {
        vlog_error("Could not load %s: %s", filename, lodepng_error_text(error));
        return NULL;
    }

    loadedImage = SDL_CreateRGBSurfaceWithFormatFrom(
        *data,
        width,
        height,
        32,
        width * 4,
#if SDL_BYTEORDER == SDL_BIG_ENDIAN
        SDL_PIXELFORMAT_RGBA8888
#else
        SDL_PIXELFORMAT_ABGR8888
#endif
    );

    return loadedImage;
}

static SDL_Surface* LoadSurfaceFromRaw(SDL_Surface* loadedImage)
{
    SDL_Surface* optimizedImage = SDL_ConvertSurfaceFormat(
        loadedImage,
        SDL_PIXELFORMAT_ARGB8888,
        0
    );
    SDL_SetSurfaceBlendMode(optimizedImage, SDL_BLENDMODE_BLEND);
    return optimizedImage;
}

SDL_Surface* LoadImageSurface(const char* filename)
{
    unsigned char* data;

    SDL_Surface* loadedImage = LoadImageRaw(filename, &data);

    SDL_Surface* optimizedImage = LoadSurfaceFromRaw(loadedImage);
    if (loadedImage != NULL)
    {
        VVV_freefunc(SDL_FreeSurface, loadedImage);
    }

    VVV_free(data);

    if (optimizedImage == NULL)
    {
        vlog_error("Image not found: %s", filename);
        SDL_assert(0 && "Image not found! See stderr.");
    }

    return optimizedImage;
}

static SDL_Texture* LoadTextureFromRaw(const char* filename, SDL_Surface* loadedImage, const TextureLoadType loadtype)
{
    if (loadedImage == NULL)
    {
        return NULL;
    }

    // Modify the surface with the load type.
    // This could be done in LoadImageRaw, however currently, surfaces are only used for
    // pixel perfect collision (which will be changed later) and the window icon.

    switch (loadtype)
    {
    case TEX_WHITE:
        SDL_LockSurface(loadedImage);
        for (int y = 0; y < loadedImage->h; y++)
        {
            for (int x = 0; x < loadedImage->w; x++)
            {
                g2dColor color = ReadPixel(loadedImage, x, y);
                color = G2D_WHITE;
                DrawPixel(loadedImage, x, y, color);
            }
        }
        SDL_UnlockSurface(loadedImage);
        break;
    case TEX_GRAYSCALE:
        SDL_LockSurface(loadedImage);
        for (int y = 0; y < loadedImage->h; y++)
        {
            for (int x = 0; x < loadedImage->w; x++)
            {
                g2dColor color = ReadPixel(loadedImage, x, y);

                // Magic numbers used for grayscaling (eyes perceive certain colors brighter than others)
                Uint8 r = G2D_GET_R(color) * 0.299;
                Uint8 g = G2D_GET_G(color) * 0.587;
                Uint8 b = G2D_GET_B(color) * 0.114;

                const double gray = SDL_floor(r + g + b + 0.5);

                color = G2D_RGB(gray, gray, gray);
                DrawPixel(loadedImage, x, y, color);
            }
        }
        SDL_UnlockSurface(loadedImage);
        break;
    default:
        break;
    }

    //Create texture from surface pixels
    SDL_Texture* texture = SDL_CreateTextureFromSurface(gameScreen.m_renderer, loadedImage);
    if (texture == NULL)
    {
        vlog_error("Failed creating texture: %s. SDL error: %s\n", filename, SDL_GetError());
    }

    return texture;
}

SDL_Texture* LoadImage(const char *filename, const TextureLoadType loadtype)
{
    unsigned char* data;

    SDL_Surface* loadedImage = LoadImageRaw(filename, &data);

    SDL_Texture* texture = LoadTextureFromRaw(filename, loadedImage, loadtype);

    if (loadedImage != NULL)
    {
        VVV_freefunc(SDL_FreeSurface, loadedImage);
    }

    VVV_free(data);

    if (texture == NULL)
    {
        vlog_error("Image not found: %s", filename);
        SDL_assert(0 && "Image not found! See stderr.");
    }

    return texture;
}

static SDL_Texture* LoadImage(const char* filename)
{
    return LoadImage(filename, TEX_COLOR);
}

static g2dImage* G2DLoadImage(const char* filename, const TextureLoadType loadtype, g2dTexFormat format)
{
    // Load Image
    unsigned int width, height;
    unsigned int error;

    unsigned char* fileIn;
    size_t length;
    FILESYSTEM_loadAssetToMemory(filename, &fileIn, &length);
    if (fileIn == NULL)
    {
        SDL_assert(0 && "Image file missing!");
        return NULL;
    }

    unsigned char* rgbaData = NULL;
    error = lodepng_decode32(&rgbaData, &width, &height, fileIn, length);
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

                const double gray = SDL_floor(r + g + b + 0.5);

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

static g2dImage* G2DLoadImage(const char* filename, g2dTexFormat format)
{
    return G2DLoadImage(filename, TEX_COLOR, format);
}

/* Any unneeded variants can be NULL */
static void G2DLoadVariants(const char* filename, g2dTexFormat format, g2dImage** colored, g2dImage** white, g2dImage** grayscale)
{
    if (colored != NULL) *colored = G2DLoadImage(filename, TEX_COLOR, format);
    if (white != NULL) *white = G2DLoadImage(filename, TEX_WHITE, G2D_CLUT4);
    if (grayscale != NULL) *grayscale = G2DLoadImage(filename, TEX_GRAYSCALE, format);
}

/* Any unneeded variants can be NULL */
static void LoadVariants(const char* filename, SDL_Texture** colored, SDL_Texture** white, SDL_Texture** grayscale)
{
    unsigned char* data;
    SDL_Surface* loadedImage = LoadImageRaw(filename, &data);

    if (colored != NULL)
    {
        *colored = LoadTextureFromRaw(filename, loadedImage, TEX_COLOR);
        if (*colored == NULL)
        {
            vlog_error("Image not found: %s", filename);
            SDL_assert(0 && "Image not found! See stderr.");
        }
    }

    if (grayscale != NULL)
    {
        *grayscale = LoadTextureFromRaw(filename, loadedImage, TEX_GRAYSCALE);
        if (*grayscale == NULL)
        {
            vlog_error("Image not found: %s", filename);
            SDL_assert(0 && "Image not found! See stderr.");
        }
    }

    if (white != NULL)
    {
        *white = LoadTextureFromRaw(filename, loadedImage, TEX_WHITE);
        if (*white == NULL)
        {
            vlog_error("Image not found: %s", filename);
            SDL_assert(0 && "Image not found! See stderr.");
        }
    }

    if (loadedImage != NULL)
    {
        VVV_freefunc(SDL_FreeSurface, loadedImage);
    }

    VVV_free(data);
}

/* The pointers `texture` and `surface` cannot be NULL */
static void LoadSprites(const char* filename, SDL_Texture** texture, SDL_Surface** surface)
{
    unsigned char* data;
    SDL_Surface* loadedImage = LoadImageRaw(filename, &data);

    *surface = LoadSurfaceFromRaw(loadedImage);
    if (*surface == NULL)
    {
        vlog_error("Image not found: %s", filename);
        SDL_assert(0 && "Image not found! See stderr.");
    }

    *texture = LoadTextureFromRaw(filename, loadedImage, TEX_WHITE);
    if (*texture == NULL)
    {
        vlog_error("Image not found: %s", filename);
        SDL_assert(0 && "Image not found! See stderr.");
    }

    if (loadedImage != NULL)
    {
        VVV_freefunc(SDL_FreeSurface, loadedImage);
    }

    VVV_free(data);
}

static void LoadSpritesTranslation(
    const char* filename,
    tinyxml2::XMLDocument* mask,
    SDL_Surface* surface_english,
    SDL_Texture** texture
) {
    /* Create a sprites texture for display in another language.
     * surface_english is used as a base. Parts of the translation (filename)
     * will replace parts of the base, as instructed in the mask XML. */

    if (surface_english == NULL)
    {
        vlog_error("LoadSpritesTranslation: English surface is NULL!");
        return;
    }

    // Make a copy of the English sprites, for working with
    SDL_Surface* working = GetSubSurface(
        surface_english,
        0, 0, surface_english->w, surface_english->h
    );
    if (working == NULL)
    {
        return;
    }

    SDL_Surface* translated;
    {
        unsigned char* data;
        SDL_Surface* loaded_image = LoadImageRaw(filename, &data);
        translated = LoadSurfaceFromRaw(loaded_image);

        VVV_freefunc(SDL_FreeSurface, loaded_image);
        VVV_free(data);
    }
    SDL_SetSurfaceBlendMode(translated, SDL_BLENDMODE_NONE);

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
        SDL_Rect src;
        src.x = x * sprite_w;
        src.y = y * sprite_h;
        src.w = pElem->IntAttribute("w", 1) * sprite_w;
        src.h = pElem->IntAttribute("h", 1) * sprite_h;

        SDL_Rect dst;
        dst.x = pElem->IntAttribute("dx", x) * sprite_w;
        dst.y = pElem->IntAttribute("dy", y) * sprite_h;

        SDL_BlitSurface(translated, &src, working, &dst);
    }

    *texture = LoadTextureFromRaw(filename, working, TEX_WHITE);

    VVV_freefunc(SDL_FreeSurface, translated);
    VVV_freefunc(SDL_FreeSurface, working);
}

void GraphicsResources::init_translations(void)
{
    VVV_freefunc(SDL_DestroyTexture, im_sprites_translated);
    VVV_freefunc(SDL_DestroyTexture, im_flipsprites_translated);

    if (loc::english_sprites)
    {
        return;
    }

    const char* langcode = loc::lang.c_str();

    const char* path_template = "lang/%s/graphics/%s";
    char path_xml[256];
    char path_sprites[256];
    char path_flipsprites[256];
    SDL_snprintf(path_xml, sizeof(path_xml), path_template, langcode, "spritesmask.xml");
    SDL_snprintf(path_sprites, sizeof(path_sprites), path_template, langcode, "sprites.png");
    SDL_snprintf(path_flipsprites, sizeof(path_flipsprites), path_template, langcode, "flipsprites.png");

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
            im_sprites_surf,
            &im_sprites_translated
        );
    }
    if (FILESYSTEM_areAssetsInSameRealDir(path_xml, path_flipsprites))
    {
        LoadSpritesTranslation(
            path_flipsprites,
            &doc_mask,
            im_flipsprites_surf,
            &im_flipsprites_translated
        );
    }
}

void GraphicsResources::init(void)
{
    g2d_tiles = G2DLoadImage("graphics/tiles.png", G2D_CLUT8);
    g2d_tiles2 = G2DLoadImage("graphics/tiles2.png", G2D_CLUT8);
    g2d_sprites = G2DLoadImage("graphics/sprites.png", TEX_WHITE, G2D_CLUT4);

    LoadVariants("graphics/tiles.png", &im_tiles, &im_tiles_white, &im_tiles_tint);
    LoadVariants("graphics/tiles2.png", &im_tiles2, NULL, &im_tiles2_tint);
    LoadVariants("graphics/entcolours.png", &im_entcolours, NULL, &im_entcolours_tint);

    LoadSprites("graphics/sprites.png", &im_sprites, &im_sprites_surf);
    LoadSprites("graphics/flipsprites.png", &im_flipsprites, &im_flipsprites_surf);

    im_tiles3 = G2DLoadImage("graphics/tiles3.png", G2D_CLUT8);
    im_teleporter = LoadImage("graphics/teleporter.png", TEX_WHITE);

    im_image0 = LoadImage("graphics/levelcomplete.png");
    im_image1 = LoadImage("graphics/minimap.png");
    im_image2 = LoadImage("graphics/covered.png");
    im_image3 = LoadImage("graphics/elephant.png", TEX_WHITE);
    im_image4 = LoadImage("graphics/gamecomplete.png");
    im_image5 = LoadImage("graphics/fliplevelcomplete.png");
    im_image6 = LoadImage("graphics/flipgamecomplete.png");
    im_image7 = LoadImage("graphics/site.png", TEX_WHITE);
    im_image8 = LoadImage("graphics/site2.png", TEX_WHITE);
    im_image9 = LoadImage("graphics/site3.png", TEX_WHITE);
    im_image10 = LoadImage("graphics/ending.png");
    im_image11 = LoadImage("graphics/site4.png", TEX_WHITE);

    im_sprites_translated = NULL;
    im_flipsprites_translated = NULL;

    init_translations();

    im_image12 = SDL_CreateTexture(gameScreen.m_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, 240, 180);

    if (im_image12 == NULL)
    {
        vlog_error("Failed to create minimap texture: %s", SDL_GetError());
        SDL_assert(0 && "Failed to create minimap texture! See stderr.");
        return;
    }

}


void GraphicsResources::destroy(void)
{
#define CLEAR(img) VVV_freefunc(SDL_DestroyTexture, img)
    CLEAR(im_tiles);
    CLEAR(im_tiles_white);
    CLEAR(im_tiles_tint);
    CLEAR(im_tiles2);
    CLEAR(im_tiles2_tint);
    if (im_tiles) g2dTexFree(&im_tiles3);
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
    CLEAR(im_flipsprites_translated);
#undef CLEAR

    VVV_freefunc(SDL_FreeSurface, im_sprites_surf);
    VVV_freefunc(SDL_FreeSurface, im_flipsprites_surf);
}

bool SaveImage(const SDL_Surface* surface, const char* filename)
{
    unsigned char* out;
    size_t outsize;
    unsigned int error;
    bool success;

    error = lodepng_encode24(
        &out, &outsize,
        (const unsigned char*) surface->pixels,
        surface->w, surface->h
    );

    if (error != 0)
    {
        vlog_error("Could not save image: %s", lodepng_error_text(error));
        return false;
    }

    success = FILESYSTEM_saveFile(filename, out, outsize);
    SDL_free(out);

    if (!success)
    {
        vlog_error("Could not save image");
    }

    return success;
}

bool SaveScreenshot(void)
{
    static time_t last_time = 0;
    static int subsecond_counter = 0;

    bool success = TakeScreenshot(&graphics.tempScreenshot);
    if (!success)
    {
        vlog_error("Could not take screenshot");
        return false;
    }

    const time_t now = time(NULL);
    const tm* date = localtime(&now);

    if (now != last_time)
    {
        last_time = now;
        subsecond_counter = 0;
    }
    subsecond_counter++;

    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d_%H-%M-%S", date);

    char name[32];
    if (subsecond_counter > 1)
    {
        SDL_snprintf(name, sizeof(name), "%s_%i", timestamp, subsecond_counter);
    }
    else
    {
        SDL_strlcpy(name, timestamp, sizeof(name));
    }

    char filename[64];
    SDL_snprintf(filename, sizeof(filename), "screenshots/1x/%s_1x.png", name);

    success = SaveImage(graphics.tempScreenshot, filename);
    if (!success)
    {
        return false;
    }

    success = UpscaleScreenshot2x(graphics.tempScreenshot, &graphics.tempScreenshot2x);
    if (!success)
    {
        vlog_error("Could not upscale screenshot to 2x");
        return false;
    }

    SDL_snprintf(filename, sizeof(filename), "screenshots/2x/%s_2x.png", name);

    success = SaveImage(graphics.tempScreenshot2x, filename);
    if (!success)
    {
        return false;
    }

    vlog_info("Saved screenshot %s", name);
    return true;
}
