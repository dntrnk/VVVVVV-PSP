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
    BUTTONGLYPHS_keyboard_set_active(false);

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
    // if (key.keymap[SDLK_LSHIFT]) //LATER
    if (false)
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
    // SDL_Event evt;

    // while (SDL_PollEvent(&evt))
    // {
    //     switch (evt.type)
    //     {

    //     case SDL_RENDER_TARGETS_RESET:
    //         gameScreen.recacheTextures();
    //         break;

    //     /* Quit Event */
    //     case SDL_QUIT:
    //         VVV_exit(0);
    //         break;
    //     }

    // }

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
}

bool KeyPoll::isDown(psp_key key)
{
    return controls_held(key);
}

bool KeyPoll::controllerWantsLeft(bool includeVert)
{
    return (    isDown(PSP_CTRL_LEFT) ||
            controls_AnalogX() < -70 ||
            (    includeVert &&
                (    isDown(PSP_CTRL_UP) ||
                    controls_AnalogY() < -70    )    )    );
}

bool KeyPoll::controllerWantsRight(bool includeVert)
{
    return (    isDown(PSP_CTRL_RIGHT) ||
            controls_AnalogX() > 70 ||
            (    includeVert &&
                (    isDown(PSP_CTRL_DOWN) ||
                    controls_AnalogY() > 70    )    )    );
}

bool KeyPoll::controllerWantsUp(void)
{
    return isDown(PSP_CTRL_UP) || controls_AnalogY() < -70;
}

bool KeyPoll::controllerWantsDown(void)
{
    return isDown(PSP_CTRL_DOWN) || controls_AnalogY() > 70;
}

bool KeyPoll::stickWantsLeft(void)
{
    return controls_AnalogX() < -70;
}

bool KeyPoll::stickWantsRight(void)
{
    return controls_AnalogX() > 70;
}

bool KeyPoll::stickWantsUp(void)
{
    return controls_AnalogY() < -70;
}

bool KeyPoll::stickWantsDown(void)
{
    return controls_AnalogY() > 70;
}