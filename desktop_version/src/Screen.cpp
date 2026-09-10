#define GAMESCREEN_DEFINITION
#include "Screen.h"

#include <SDL.h>

#include "Alloc.h"
#include "Constants.h"
#include "CustomLevels.h"
#include "Enums.h"
#include "Exit.h"
#include "FileSystemUtils.h"
#include "Game.h"
#include "Graphics.h"
#include "GraphicsUtil.h"
#include "GraphicsResources.h"
#include "InterimVersion.h"
#include "Map.h"
#include "Render.h"
#include "Vlogging.h"

void ScreenSettings_default(struct ScreenSettings* _this)
{
    _this->windowDisplay = 0;
    _this->windowWidth = 320;
    _this->windowHeight = 240;
    _this->fullscreen = true;
    _this->useVsync = true; // Now that uncapped is the default...
    _this->scalingMode = SCALING_INTEGER;
    _this->linearFilter = false;
    _this->badSignal = false;
}

void Screen::init(const struct ScreenSettings* settings)
{
    m_window = NULL;
    m_renderer = NULL;
    windowDisplay = settings->windowDisplay;
    windowWidth = settings->windowWidth;
    windowHeight = settings->windowHeight;
    isWindowed = !settings->fullscreen;
    scalingMode = settings->scalingMode;
    isFiltered = settings->linearFilter;
    badSignalEffect = settings->badSignal;
    vsync = settings->useVsync;

    g2dInit();
}

void Screen::destroy(void)
{
    /* Order matters! */
    // VVV_freefunc(SDL_DestroyRenderer, m_renderer);
    // VVV_freefunc(SDL_DestroyWindow, m_window);

    g2dTerm();
}

void Screen::GetSettings(struct ScreenSettings* settings)
{
    settings->windowDisplay = windowDisplay;
    settings->windowWidth = windowWidth;
    settings->windowHeight = windowHeight;

    settings->fullscreen = !isWindowed;
    settings->useVsync = vsync;
    settings->scalingMode = scalingMode;
    settings->linearFilter = isFiltered;
    settings->badSignal = badSignalEffect;
}

void Screen::RenderPresent(void)
{
    // SDL_RenderPresent(m_renderer);
    graphics.clear();
    graphics.fill_rect(-80, -16, 480, 16, G2D_BLACK);
    graphics.fill_rect(-80, 240, 480, 16, G2D_BLACK);
    graphics.fill_rect(-80, 0, 80, 240, G2D_BLACK);
    graphics.fill_rect(320, 0, 80, 240, G2D_BLACK);
    g2dHelperFlip();
}

void Screen::recacheTextures(void)
{
    // Fix for d3d9, which clears target textures sometimes (ex. toggling vsync, switching fullscreen, etc...)

    // Signal cached textures to be redrawn fully
    graphics.backgrounddrawn = false;
    graphics.foregrounddrawn = false;
    graphics.towerbg.tdrawback = true;
    graphics.titlebg.tdrawback = true;

    if (game.gamestate == MAPMODE || game.ingame_titlemode)
    {
        // Redraw the cached gameplay texture if we're in the map screen.
        // Additionally, reset alpha so things don't jitter when re-entering gameplay.
        float oldAlpha = graphics.alpha;
        graphics.alpha = 0;
        gamerender();
        graphics.alpha = oldAlpha;
    }

    if (map.custommode)
    {
        // If we're in a custom level, regenerate the minimap, which also got cleared.
        cl.generatecustomminimap();
    }
}

bool Screen::isForcedFullscreen(void)
{
    /* This is just a check to see if we're on a desktop or tenfoot setup.
     * If you're working on a tenfoot-only build, add a def that always
     * returns true!
     */
    return SDL_GetHintBoolean("SteamTenfoot", SDL_FALSE);
}
