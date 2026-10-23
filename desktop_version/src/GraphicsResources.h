#ifndef GRAPHICSRESOURCES_H
#define GRAPHICSRESOURCES_H

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
    void preserve(void);
    void init(void);
    void destroy(void);

    void init_translations(void);

    g2dImage* im_tiles = NULL;
    g2dImage* im_tiles_white = NULL;
    g2dImage* im_tiles_tint = NULL;
    g2dImage* im_tiles2 = NULL;
    g2dImage* im_tiles2_tint = NULL;
    g2dImage* im_tiles3 = NULL;
    g2dImage* im_entcolours = NULL;
    g2dImage* im_entcolours_tint = NULL;
    g2dImage* im_sprites = NULL;
    g2dImage* im_flipsprites = NULL;
    g2dImage* im_teleporter = NULL;
    g2dImage* im_image1 = NULL;
    g2dImage* im_image2 = NULL;
    g2dImage* im_image3 = NULL;
    g2dImage* im_image7 = NULL;
    g2dImage* im_image8 = NULL;
    g2dImage* im_image9 = NULL;
    g2dImage* im_image10 = NULL;
    g2dImage* im_image12 = NULL;
    g2dImage* im_image13 = NULL;

    g2dImage* im_sprites_translated = NULL;
    g2dImage* im_flipsprites_translated = NULL;

    bool sprites_translated_valid = false;
    bool flipsprites_translated_valid = false;
};

extern uint32_t sprites_collision_surface_normal[512][16];
extern uint32_t sprites_collision_surface_flipped[512][16];

bool sprites_collision_surface_get_bit(uint32_t (*collision_surface)[16], int x, int y);
void sprites_collision_surface_set_bit(uint32_t (*collision_surface)[16], int x, int y);
void sprites_collision_surface_clear_bit(uint32_t (*collision_surface)[16], int x, int y);

g2dImage* LoadImage(const char *filename, TextureLoadType loadtype, g2dTexFormat format, uint32_t (*collision_surface)[16] = NULL);
g2dImage* LoadFont(const char *filename, const TextureLoadType loadtype);
g2dImage* LoadImage(const char *filename, g2dTexFormat format);

#endif /* GRAPHICSRESOURCES_H */
