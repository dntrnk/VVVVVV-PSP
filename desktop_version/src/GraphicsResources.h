#ifndef GRAPHICSRESOURCES_H
#define GRAPHICSRESOURCES_H

#include <SDL.h>

#include "glib2d_helper.h"

enum TextureLoadType
{
    TEX_COLOR,
    TEX_WHITE,
    TEX_GRAYSCALE
};

class GraphicsResources
{
public:
    void init(void);
    void destroy(void);

    void init_translations(void);

    SDL_Surface* im_sprites_surf;
    SDL_Surface* im_flipsprites_surf;

    g2dImage* im_tiles;
    g2dImage* im_tiles_white;
    g2dImage* im_tiles_tint;
    g2dImage* im_tiles2;
    g2dImage* im_tiles2_tint;
    g2dImage* im_tiles3;
    g2dImage* im_entcolours;
    g2dImage* im_entcolours_tint;
    SDL_Texture* im_sprites;
    SDL_Texture* im_flipsprites;
    SDL_Texture* im_teleporter;
    g2dImage* im_image0;
    g2dImage* im_image1;
    g2dImage* im_image2;
    g2dImage* im_image3;
    g2dImage* im_image4;
    g2dImage* im_image5;
    g2dImage* im_image6;
    g2dImage* im_image7;
    g2dImage* im_image8;
    g2dImage* im_image9;
    g2dImage* im_image10;
    g2dImage* im_image11;
    g2dImage* im_image12;

    SDL_Texture* im_sprites_translated;
    SDL_Texture* im_flipsprites_translated;

    g2dImage* g2d_sprites;
};

SDL_Surface* LoadImageSurface(const char* filename);
SDL_Texture* LoadImage(const char *filename, TextureLoadType loadtype);
g2dImage* G2DLoadImage(const char *filename, TextureLoadType loadtype, g2dTexFormat format);
g2dImage* G2DLoadImage(const char *filename, g2dTexFormat format);

bool SaveImage(const SDL_Surface* surface, const char* filename);
bool SaveScreenshot(void);

#endif /* GRAPHICSRESOURCES_H */
