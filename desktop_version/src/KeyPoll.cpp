#define KEY_DEFINITION
#include "KeyPoll.h"

#include <pspgu.h>
#include <psputility.h>

#include <string.h>

#include "Alloc.h"
#include "ButtonGlyphs.h"
#include "Constants.h"
#include "Editor.h"
#include "Exit.h"
#include "Game.h"
#include "GlitchrunnerMode.h"
#include "Graphics.h"
#include "GraphicsUtil.h"
#include "Localization.h"
#include "LocalizationMaint.h"
#include "LocalizationStorage.h"
#include "Music.h"
#include "Screen.h"
#include "UTF8.h"
#include "UtilityClass.h"
#include "Vlogging.h"

int inline KeyPoll::getThreshold(void)
{
    switch (sensitivity)
    {
    case 0:
        return 28000;
    case 1:
        return 16000;
    case 2:
        return 8000;
    case 3:
        return 4000;
    case 4:
        return 2000;
    }

    return 8000;

}

KeyPoll::KeyPoll(void)
{
    xVel = 0;
    yVel = 0;
    // 0..5
    sensitivity = 2;

    keybuffer = "";
    imebuffer = "";
    imebuffer_start = 0;
    imebuffer_length = 0;
    leftbutton=0; rightbutton=0; middlebutton=0;
    mousex = 0;
    mousey = 0;
    resetWindow = 0;
    pressedbackspace=false;

    linealreadyemptykludge = false;

    isActive = true;

    osk_active = false;
    osk_done = false;
    osk_just_closed = false;
    osk_result = "";
    memset(osk_intext, 0, sizeof(osk_intext));
    memset(osk_outtext, 0, sizeof(osk_outtext));

    osk_status = sceUtilityOskGetStatus();
}

void KeyPoll::enabletextentry(void)
{
    if (osk_active) return;
    osk_active = true;

    imebuffer = "";
    imebuffer_start = 0;
    imebuffer_length = 0;

    memset(osk_intext, 0, sizeof(osk_intext));
    memset(osk_outtext, 0, sizeof(osk_outtext));

    for (size_t i = 0; i < keybuffer.size() && i < 255; i++)
    {
        osk_intext[i] = (uint16_t)(unsigned char)keybuffer[i];
    }

    SceUtilityOskData oskData;
    memset(&oskData, 0, sizeof(oskData));
    oskData.language = PSP_UTILITY_OSK_LANGUAGE_ENGLISH;
    oskData.lines = 1;
    oskData.unk_24 = 0;
    oskData.inputtype = PSP_UTILITY_OSK_INPUTTYPE_ALL;
    oskData.desc = NULL;
    oskData.intext = osk_intext;
    oskData.outtext = osk_outtext;
    oskData.outtextlength = sizeof(osk_outtext) / sizeof(osk_outtext[0]);
    oskData.outtextlimit = 255;

    SceUtilityOskParams oskParams;
    memset(&oskParams, 0, sizeof(oskParams));
    oskParams.base.size = sizeof(oskParams);
    oskParams.base.language = PSP_UTILITY_OSK_LANGUAGE_ENGLISH;
    oskParams.base.buttonSwap = PSP_UTILITY_ACCEPT_CROSS;
    oskParams.base.graphicsThread = 0x11;
    oskParams.base.accessThread = 0x13;
    oskParams.base.fontThread = 0x12;
    oskParams.base.soundThread = 0x10;
    oskParams.datacount = 1;
    oskParams.data = &oskData;

    sceUtilityOskInitStart(&oskParams);

    int done = 0;
    while (!done)
    {
        // Тут надо бы нарисовать фон, но можно и просто clear
        g2dClear(G2D_BLACK);

        sceGuFinish();
        sceGuSync(0, 0);

        switch (sceUtilityOskGetStatus())
        {
        case PSP_UTILITY_DIALOG_INIT:
            break;
        case PSP_UTILITY_DIALOG_VISIBLE:
            sceUtilityOskUpdate(1);
            break;
        case PSP_UTILITY_DIALOG_QUIT:
            sceUtilityOskShutdownStart();
            break;
        case PSP_UTILITY_DIALOG_FINISHED:
            break;
        case PSP_UTILITY_DIALOG_NONE:
            done = 1;
            break;
        default:
            break;
        }

        g2dFlip(G2D_VSYNC);
    }

    keybuffer.clear();
    for (int i = 0; osk_outtext[i] != 0 && i < 256; i++)
    {
        keybuffer += (char)(osk_outtext[i] & 0xFF);
    }

    osk_active = false;
    osk_done = true;
    osk_just_closed = true;
}

void KeyPoll::disabletextentry(void)
{
    imebuffer = "";
    imebuffer_start = 0;
    imebuffer_length = 0;

    if (osk_active)
    {
        sceUtilityOskShutdownStart();
        osk_active = false;
    }
}

bool KeyPoll::textentry(void)
{
    return osk_active;
}

static int changemousestate(
    int timeout,
    const bool show,
    const bool hide
) {
    int prev;
    int new_;

    if (timeout > 0)
    {
        return --timeout;
    }

    /* If we want to both show and hide at the same time, prioritize showing */
    if (show)
    {
        new_ = SDL_ENABLE;
    }
    else if (hide)
    {
        new_ = SDL_DISABLE;
    }
    else
    {
        return timeout;
    }

    prev = SDL_ShowCursor(SDL_QUERY);

    if (prev == new_)
    {
        return timeout;
    }

    SDL_ShowCursor(new_);

    switch (new_)
    {
    case SDL_DISABLE:
        timeout = 0;
        break;
    case SDL_ENABLE:
        timeout = 30;
        break;
    }

    return timeout;
}

/* Also used in Input.cpp. */
void recomputetextboxes(void);

bool cycle_language(bool should_recompute_textboxes)
{
    extern KeyPoll key;

    if (game.gamestate == TITLEMODE
    && game.currentmenuname == Menu::translator_options_cutscenetest)
    {
        /* Unfortunately, despite how it may appear to be working, the options
         * are actually language-specific, and the order could be totally
         * different between languages too. So we can't cycle in this menu. */
        music.playef(Sound_CRY);
        return should_recompute_textboxes;
    }
    if (game.translator_cutscene_test)
    {
        /* Refuse cycling here for similar reasons, even if it seems like it's
         * working. The text boxes are based off of the language XML and
         * could be completely different between languages. */
        music.playef(Sound_CRY);
        return should_recompute_textboxes;
    }

    int i = loc::languagelist_curlang;
    if (key.keymap[SDLK_LSHIFT])
    {
        /* Backwards */
        i--;
    }
    else
    {
        /* Forwards */
        i++;
    }
    if (!loc::languagelist.empty())
    {
        i = POS_MOD(i, (int) loc::languagelist.size());

        loc::languagelist_curlang = i;
        loc::lang = loc::languagelist[i].code;
        loc::loadtext(false);
        graphics.grphx.init_translations();

        should_recompute_textboxes = true;
    }

    if (game.gamestate == TITLEMODE
    || (game.gamestate == EDITORMODE && ed.state == EditorState_MENU))
    {
        if (game.currentmenuname == Menu::translator_options_limitscheck)
        {
            loc::local_limits_check();
        }

        int temp = game.menucountdown;
        game.createmenu(game.currentmenuname, true);
        game.menucountdown = temp;

        if (game.currentmenuname == Menu::language)
        {
            game.currentmenuoption = i;
        }
    }

    return should_recompute_textboxes;
}

void KeyPoll::Poll(void)
{
    static int raw_mousex = 0;
    static int raw_mousey = 0;
    static int mousetoggletimeout = 0;
    bool showmouse = false;
    bool hidemouse = false;
    bool altpressed = false;
    bool fullscreenkeybind = false;
    SDL_GameController *controller = NULL;
    SDL_Event evt;
    bool should_recompute_textboxes = false;
    bool active_input_device_changed = false;
    bool keyboard_was_active = BUTTONGLYPHS_keyboard_is_active();
    while (SDL_PollEvent(&evt))
    {
        switch (evt.type)
        {
        /* Keyboard Input */
        case SDL_KEYDOWN:
        {
            keymap[evt.key.keysym.sym] = true;

            if (evt.key.keysym.sym == SDLK_BACKSPACE)
            {
                pressedbackspace = true;
            }

#ifdef __APPLE__ /* OSX prefers the command keys over the alt keys. -flibit */
            altpressed = keymap[SDLK_LGUI] || keymap[SDLK_RGUI];
#else
            altpressed = keymap[SDLK_LALT] || keymap[SDLK_RALT];
#endif
            bool returnpressed = evt.key.keysym.sym == SDLK_RETURN;
            bool fpressed = evt.key.keysym.sym == SDLK_f;
            bool f11pressed = evt.key.keysym.sym == SDLK_F11;
            if ((altpressed && (returnpressed || fpressed)) || f11pressed)
            {
                fullscreenkeybind = true;
            }

            if (loc::show_translator_menu && evt.key.keysym.sym == SDLK_F8 && !evt.key.repeat)
            {
                if (keymap[SDLK_LCTRL])
                {
                    /* Debug keybind to cycle language. */
                    should_recompute_textboxes = cycle_language(should_recompute_textboxes);
                }
                else
                {
                    /* Reload language files */
                    loc::loadtext(false);
                    graphics.grphx.init_translations();
                    music.playef(Sound_COIN);
                }
            }
            
            BUTTONGLYPHS_keyboard_set_active(true);

        }
        case SDL_KEYUP:
            keymap[evt.key.keysym.sym] = false;
            if (evt.key.keysym.sym == SDLK_BACKSPACE)
            {
                pressedbackspace = false;
            }
            break;

        /* Mouse Input */
        case SDL_MOUSEMOTION:
            raw_mousex = evt.motion.x;
            raw_mousey = evt.motion.y;
            break;
        case SDL_MOUSEBUTTONDOWN:
            switch (evt.button.button)
            {
            case SDL_BUTTON_LEFT:
                raw_mousex = evt.button.x;
                raw_mousey = evt.button.y;
                leftbutton = 1;
                break;
            case SDL_BUTTON_RIGHT:
                raw_mousex = evt.button.x;
                raw_mousey = evt.button.y;
                rightbutton = 1;
                break;
            case SDL_BUTTON_MIDDLE:
                raw_mousex = evt.button.x;
                raw_mousey = evt.button.y;
                middlebutton = 1;
                break;
            }
            break;
        case SDL_MOUSEBUTTONUP:
            switch (evt.button.button)
            {
            case SDL_BUTTON_LEFT:
                raw_mousex = evt.button.x;
                raw_mousey = evt.button.y;
                leftbutton=0;
                break;
            case SDL_BUTTON_RIGHT:
                raw_mousex = evt.button.x;
                raw_mousey = evt.button.y;
                rightbutton=0;
                break;
            case SDL_BUTTON_MIDDLE:
                raw_mousex = evt.button.x;
                raw_mousey = evt.button.y;
                middlebutton=0;
                break;
            }
            break;

        /* Controller Input */
        case SDL_CONTROLLERBUTTONDOWN:
            buttonmap[(SDL_GameControllerButton) evt.cbutton.button] = true;
            BUTTONGLYPHS_keyboard_set_active(false);

            controller = controllers[evt.cbutton.which];
            BUTTONGLYPHS_update_layout(controller);
            break;
        case SDL_CONTROLLERBUTTONUP:
            buttonmap[(SDL_GameControllerButton) evt.cbutton.button] = false;
            break;
        case SDL_CONTROLLERAXISMOTION:
        {
            const int threshold = getThreshold();
            switch (evt.caxis.axis)
            {
            case SDL_CONTROLLER_AXIS_LEFTX:
                if (    evt.caxis.value > -threshold &&
                    evt.caxis.value < threshold    )
                {
                    xVel = 0;
                }
                else
                {
                    xVel = (evt.caxis.value > 0) ? 1 : -1;
                }
                break;
            case SDL_CONTROLLER_AXIS_LEFTY:
                if (    evt.caxis.value > -threshold &&
                    evt.caxis.value < threshold    )
                {
                    yVel = 0;
                }
                else
                {
                    yVel = (evt.caxis.value > 0) ? 1 : -1;
                }
                break;
            }
            BUTTONGLYPHS_keyboard_set_active(false);

            controller = controllers[evt.caxis.which];
            BUTTONGLYPHS_update_layout(controller);
            break;
        }
        case SDL_CONTROLLERDEVICEADDED:
        {
            controller = SDL_GameControllerOpen(evt.cdevice.which);
            vlog_info(
                "Opened SDL_GameController ID #%i, %s",
                evt.cdevice.which,
                SDL_GameControllerName(controller)
            );
            controllers[SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(controller))] = controller;
            BUTTONGLYPHS_keyboard_set_active(false);
            BUTTONGLYPHS_update_layout(controller);
            break;
        }
        case SDL_CONTROLLERDEVICEREMOVED:
        {
            controller = controllers[evt.cdevice.which];
            controllers.erase(evt.cdevice.which);
            vlog_info("Closing %s", SDL_GameControllerName(controller));
            SDL_GameControllerClose(controller);
            if (controllers.empty())
            {
                BUTTONGLYPHS_keyboard_set_active(true);
            }
            break;
        }

        case SDL_RENDER_TARGETS_RESET:
            gameScreen.recacheTextures();
            break;

        /* Quit Event */
        case SDL_QUIT:
            VVV_exit(0);
            break;
        }

        switch (evt.type)
        {
        case SDL_KEYDOWN:
            if (evt.key.repeat == 0)
            {
                hidemouse = true;
            }
            break;
        case SDL_CONTROLLERBUTTONDOWN:
        case SDL_CONTROLLERAXISMOTION:
            hidemouse = true;
            break;
        case SDL_MOUSEMOTION:
        case SDL_MOUSEBUTTONDOWN:
            showmouse = true;
            break;
        }
    }

    mousetoggletimeout = changemousestate(
        mousetoggletimeout,
        showmouse,
        hidemouse
    );

    VVV_Rect rect = {0, 0, 320, 240};

    int window_width = 320;
    int window_height = 240;

    int scaled_window_width = 320;
    int scaled_window_height = 240;

    float scale_x = (float)window_width / (float)scaled_window_width;
    float scale_y = (float)window_height / (float)scaled_window_height;

    // Use screen stretch information to modify the coordinates (as we implement stretching manually)
    mousex = ((raw_mousex * scale_x) - rect.x) * SCREEN_WIDTH_PIXELS / rect.w;
    mousey = ((raw_mousey * scale_y) - rect.y) * SCREEN_HEIGHT_PIXELS / rect.h;

    active_input_device_changed = keyboard_was_active != BUTTONGLYPHS_keyboard_is_active();
    should_recompute_textboxes |= active_input_device_changed;
    if (should_recompute_textboxes)
    {
        recomputetextboxes();
    }
}

bool KeyPoll::isDown(SDL_Keycode key)
{
    return keymap[key];
}

bool KeyPoll::isDown(std::vector<SDL_GameControllerButton> buttons)
{
    for (size_t i = 0; i < buttons.size(); i += 1)
    {
        if (buttonmap[buttons[i]])
        {
            return true;
        }
    }
    return false;
}

bool KeyPoll::isDown(SDL_GameControllerButton button)
{
    return buttonmap[button];
}

bool KeyPoll::controllerButtonDown(void)
{
    for (
        SDL_GameControllerButton button = SDL_CONTROLLER_BUTTON_A;
        button < SDL_CONTROLLER_BUTTON_DPAD_UP;
        button = (SDL_GameControllerButton) (button + 1)
    ) {
        if (isDown(button))
        {
            return true;
        }
    }
    return false;
}

bool KeyPoll::controllerWantsLeft(bool includeVert)
{
    return (    buttonmap[SDL_CONTROLLER_BUTTON_DPAD_LEFT] ||
            xVel < 0 ||
            (    includeVert &&
                (    buttonmap[SDL_CONTROLLER_BUTTON_DPAD_UP] ||
                    yVel < 0    )    )    );
}

bool KeyPoll::controllerWantsRight(bool includeVert)
{
    return (    buttonmap[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] ||
            xVel > 0 ||
            (    includeVert &&
                (    buttonmap[SDL_CONTROLLER_BUTTON_DPAD_DOWN] ||
                    yVel > 0    )    )    );
}

bool KeyPoll::controllerWantsUp(void)
{
    return buttonmap[SDL_CONTROLLER_BUTTON_DPAD_UP] || yVel < 0;
}

bool KeyPoll::controllerWantsDown(void)
{
    return buttonmap[SDL_CONTROLLER_BUTTON_DPAD_DOWN] || yVel > 0;
}

bool KeyPoll::stickWantsLeft(void)
{
    return xVel < 0;
}

bool KeyPoll::stickWantsRight(void)
{
    return xVel > 0;
}

bool KeyPoll::stickWantsUp(void)
{
    return yVel < 0;
}

bool KeyPoll::stickWantsDown(void)
{
    return yVel > 0;
}