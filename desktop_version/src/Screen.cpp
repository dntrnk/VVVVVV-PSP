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
    _this->windowWidth = SCREEN_WIDTH_PIXELS * 2;
    _this->windowHeight = SCREEN_HEIGHT_PIXELS * 2;
    _this->fullscreen = false;
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

    // Uncomment this next line when you need to debug -flibit
    // SDL_SetHintWithPriority(SDL_HINT_RENDER_DRIVER, "software", SDL_HINT_OVERRIDE);

    // SDL_CreateWindowAndRenderer(480, 272, 0, &m_window, &m_renderer);

    // m_window = SDL_CreateWindow(
    //     "VVVVVV",
    //     SDL_WINDOWPOS_CENTERED_DISPLAY(windowDisplay),
    //     SDL_WINDOWPOS_CENTERED_DISPLAY(windowDisplay),
    //     SCREEN_WIDTH_PIXELS * 2,
    //     SCREEN_HEIGHT_PIXELS * 2,
    //     SDL_WINDOW_HIDDEN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI
    // );

    // if (m_window == NULL)
    // {
    //     vlog_error("Could not create window: %s", SDL_GetError());
    //     VVV_exit(1);
    // }

    // m_renderer = SDL_CreateRenderer(m_window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE);

    // if (m_renderer == NULL)
    // {
    //     vlog_error("Could not create renderer: %s", SDL_GetError());
    //     VVV_exit(1);
    // }

    // SDL_RenderSetVSync(m_renderer, (int) vsync);

    // SDL_SetWindowMinimumSize(m_window, 480, 272);

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
    windowDisplay = SDL_GetWindowDisplayIndex(m_window);
    if (windowDisplay < 0)
    {
        vlog_error("Error: could not get display index: %s", SDL_GetError());
        windowDisplay = 0;
    }
    settings->windowDisplay = windowDisplay;
    settings->windowWidth = windowWidth;
    settings->windowHeight = windowHeight;

    settings->fullscreen = !isWindowed;
    settings->useVsync = vsync;
    settings->scalingMode = scalingMode;
    settings->linearFilter = isFiltered;
    settings->badSignal = badSignalEffect;
}

static void constrain_to_desktop(int display_index, int* width, int* height)
{
    SDL_DisplayMode display_mode = {};
    int success = SDL_GetDesktopDisplayMode(display_index, &display_mode);
    if (success != 0)
    {
        vlog_error("Could not get desktop display mode: %s", SDL_GetError());
        return;
    }

    while ((*width > display_mode.w || *height > display_mode.h)
    && *width > SCREEN_WIDTH_PIXELS && *height > SCREEN_HEIGHT_PIXELS)
    {
        // We are too big, take away one multiple
        *width -= SCREEN_WIDTH_PIXELS;
        *height -= SCREEN_HEIGHT_PIXELS;
    }
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

void Screen::toggleLinearFilter(void)
{
    isFiltered = !isFiltered;

    SDL_DestroyTexture(graphics.gameTexture);
    SDL_DestroyTexture(graphics.tempShakeTexture);

    graphics.gameTexture = SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET,
        SCREEN_WIDTH_PIXELS,
        SCREEN_HEIGHT_PIXELS
    );

    graphics.tempShakeTexture = SDL_CreateTexture(
        m_renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_TARGET,
        SCREEN_WIDTH_PIXELS,
        SCREEN_HEIGHT_PIXELS
    );

    if (graphics.gameTexture == NULL)
    {
        vlog_error("Could not create game texture: %s", SDL_GetError());
        return;
    }

    if (graphics.tempShakeTexture == NULL)
    {
        vlog_error("Could not create temp shake texture: %s", SDL_GetError());
        return;
    }

    SDL_SetTextureScaleMode(
        graphics.gameTexture,
        isFiltered ? SDL_ScaleModeLinear : SDL_ScaleModeNearest
    );

    SDL_SetTextureScaleMode(
        graphics.tempShakeTexture,
        isFiltered ? SDL_ScaleModeLinear : SDL_ScaleModeNearest
    );
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
