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

static void utf16_to_utf8(const uint16_t* src, char* dst, size_t dst_size)
{
    size_t j = 0;
    for (size_t i = 0; src[i] != 0 && j < dst_size - 4; i++)
    {
        uint16_t cp = src[i];

        if (cp < 0x80)
        {
            // ASCII (1 byte)
            dst[j++] = (char)cp;
        }
        else if (cp < 0x800)
        {
            // 2 bytes (cyrillic, latin with diacritics)
            dst[j++] = (char)(0xC0 | (cp >> 6));
            dst[j++] = (char)(0x80 | (cp & 0x3F));
        }
        else
        {
            // 3 bytes (CJK, rare symbols)
            dst[j++] = (char)(0xE0 | (cp >> 12));
            dst[j++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            dst[j++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    dst[j] = 0;
}

static void utf8_to_utf16(const char* src, uint16_t* dst, size_t dst_size)
{
    if (src == NULL || dst_size == 0)
    {
        if (dst_size > 0) dst[0] = 0;
        return;
    }

    size_t j = 0;
    for (size_t i = 0; src[i] != '\0' && j < dst_size - 1; )
    {
        unsigned char c = (unsigned char)src[i];

        if (c < 0x80)
        {
            // ASCII
            dst[j++] = c;
            i += 1;
        }
        else if ((c & 0xE0) == 0xC0)
        {
            // 2-byte UTF-8 (cyrillic, latin with diacritic)
            if (src[i + 1] == '\0') break;
            uint16_t cp = ((c & 0x1F) << 6)
                        | ((unsigned char)src[i + 1] & 0x3F);
            dst[j++] = cp;
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0)
        {
            // 3-byte UTF-8
            if (src[i + 2] == '\0') break;
            uint16_t cp = ((c & 0x0F) << 12)
                        | (((unsigned char)src[i + 1] & 0x3F) << 6)
                        | ((unsigned char)src[i + 2] & 0x3F);
            dst[j++] = cp;
            i += 3;
        }
        else
        {
            // 4-byte UTF-8 (emoji) — PSP OSK don't support this, so let's put a '?' there
            dst[j++] = '?';
            i += 4;
        }
    }
    dst[j] = 0;
}

static void capitalize_and_convert(const char* src, uint16_t* dst, size_t dst_size)
{
    utf8_to_utf16(src, dst, dst_size);

    if (dst[0] == 0) return;

    uint16_t cp = dst[0];
    if (cp >= 'a' && cp <= 'z') cp = cp - 'a' + 'A';
    else if (cp >= 0x0430 && cp <= 0x044F) cp = cp - 0x20; // а-я -> А-Я
    else if (cp == 0x0451) cp = 0x0401; // ё -> Ё
    dst[0] = cp;
}

void KeyPoll::enabletextentry(const char* desc /*= NULL*/)
{
    if (osk_active) return;
    osk_active = true;

    imebuffer = "";
    imebuffer_start = 0;
    imebuffer_length = 0;

    memset(osk_intext, 0, sizeof(osk_intext));
    memset(osk_outtext, 0, sizeof(osk_outtext));

    utf8_to_utf16(keybuffer.c_str(), osk_intext, sizeof(osk_intext) / sizeof(osk_intext[0]));

    int sys_lang = PSP_UTILITY_OSK_LANGUAGE_ENGLISH;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_LANGUAGE, &sys_lang);

    int button_swap = 0;
    sceUtilityGetSystemParamInt(PSP_SYSTEMPARAM_ID_INT_UNKNOWN, &button_swap);

    static uint16_t osk_desc_buffer[128];
    memset(osk_desc_buffer, 0, sizeof(osk_desc_buffer));

    if (desc != NULL)
    {
        capitalize_and_convert(desc, osk_desc_buffer, 128);
    }

    SceUtilityOskData oskData;
    memset(&oskData, 0, sizeof(oskData));
    oskData.language = sys_lang;
    oskData.lines = 1;
    oskData.unk_24 = 0;

    oskData.inputtype = PSP_UTILITY_OSK_INPUTTYPE_LATIN_LOWERCASE |
                        PSP_UTILITY_OSK_INPUTTYPE_LATIN_UPPERCASE |
                        PSP_UTILITY_OSK_INPUTTYPE_RUSSIAN_LOWERCASE |
                        PSP_UTILITY_OSK_INPUTTYPE_RUSSIAN_UPPERCASE |
                        PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_HIRAGANA |
                        PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_HALF_KATAKANA |
                        PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_KATAKANA |
                        PSP_UTILITY_OSK_INPUTTYPE_JAPANESE_KANJI |
                        PSP_UTILITY_OSK_INPUTTYPE_KOREAN;
    
    oskData.desc = (desc != NULL) ? osk_desc_buffer : NULL;
    oskData.intext = osk_intext;
    oskData.outtext = osk_outtext;
    oskData.outtextlength = sizeof(osk_outtext) / sizeof(osk_outtext[0]);
    oskData.outtextlimit = 127;

    SceUtilityOskParams oskParams;
    memset(&oskParams, 0, sizeof(oskParams));
    oskParams.base.size = sizeof(oskParams);
    oskParams.base.language = sys_lang;
    oskParams.base.buttonSwap = button_swap;
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

    char utf8_out[385];  // 256 symbols × 3 byte max + extra
    utf16_to_utf8(osk_outtext, utf8_out, sizeof(utf8_out));
    keybuffer = utf8_out;

    osk_active = false;
    osk_done = true;
    osk_just_closed = true;
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