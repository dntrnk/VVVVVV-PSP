#ifndef SCREEN_H
#define SCREEN_H

#include <SDL.h>

#include "glib2d_helper.h"

#include "ScreenSettings.h"

class Screen
{
public:
    void init(const struct ScreenSettings* settings);
    void destroy(void);

    void GetSettings(struct ScreenSettings* settings);

    void RenderPresent(void);
    
    void toggleLinearFilter(void);

    void recacheTextures(void);

    bool isForcedFullscreen(void);

    int windowDisplay;
    int windowWidth;
    int windowHeight;
    bool isWindowed;
    bool isFiltered;
    bool badSignalEffect;
    int scalingMode;
    bool vsync;

    SDL_Window *m_window;
    SDL_Renderer *m_renderer;
};

#ifndef GAMESCREEN_DEFINITION
extern Screen gameScreen;
#endif

#endif /* SCREEN_H */
