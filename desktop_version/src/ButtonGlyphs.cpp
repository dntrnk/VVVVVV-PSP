#include "ButtonGlyphs.h"

#include <cassert>

#include "Game.h"
#include "Localization.h"
#include "UTF8.h"

extern "C"
{

typedef enum
{
    GLYPH_NINTENDO_DECK_A, // Note that for the Deck, the icons are same as Nintendo but the layout is the same as Xbox
    GLYPH_NINTENDO_DECK_B,
    GLYPH_NINTENDO_DECK_X,
    GLYPH_NINTENDO_DECK_Y,
    GLYPH_NINTENDO_PLUS,
    GLYPH_NINTENDO_MINUS,
    GLYPH_NINTENDO_L,
    GLYPH_NINTENDO_R,
    GLYPH_NINTENDO_ZL,
    GLYPH_NINTENDO_ZR,
    GLYPH_NINTENDO_XBOX_LSTICK,
    GLYPH_NINTENDO_XBOX_RSTICK,
    GLYPH_NINTENDO_SL,
    GLYPH_NINTENDO_SR,
    GLYPH_GENERIC_L,
    GLYPH_GENERIC_R,

    GLYPH_PLAYSTATION_CIRCLE,
    GLYPH_PLAYSTATION_CROSS,
    GLYPH_PLAYSTATION_TRIANGLE,
    GLYPH_PLAYSTATION_SQUARE,
    GLYPH_PLAYSTATION_START,
    GLYPH_PLAYSTATION_OPTIONS,
    GLYPH_PLAYSTATION_DECK_L1,
    GLYPH_PLAYSTATION_DECK_R1,
    GLYPH_PLAYSTATION_DECK_L2,
    GLYPH_PLAYSTATION_DECK_R2,
    GLYPH_PLAYSTATION_DECK_L3,
    GLYPH_PLAYSTATION_DECK_R3,
    GLYPH_DECK_L4,
    GLYPH_DECK_R4,
    GLYPH_DECK_L5,
    GLYPH_DECK_R5,

    GLYPH_XBOX_B,
    GLYPH_XBOX_A,
    GLYPH_XBOX_Y,
    GLYPH_XBOX_X,
    GLYPH_XBOX_DECK_VIEW,
    GLYPH_XBOX_DECK_MENU,
    GLYPH_XBOX_LB,
    GLYPH_XBOX_RB,
    GLYPH_XBOX_LT,
    GLYPH_XBOX_RT,
    GLYPH_NINTENDO_GENERIC_ACTIONRIGHT,
    GLYPH_NINTENDO_GENERIC_ACTIONDOWN,
    GLYPH_NINTENDO_GENERIC_ACTIONUP,
    GLYPH_NINTENDO_GENERIC_ACTIONLEFT,
    GLYPH_NINTENDO_GENERIC_STICK,
    GLYPH_UNKNOWN,

    /* Added after 2.4 */
    GLYPH_NINTENDO_GAMECUBE_A,
    GLYPH_NINTENDO_GAMECUBE_B,
    GLYPH_NINTENDO_GAMECUBE_X,
    GLYPH_NINTENDO_GAMECUBE_Y,
    GLYPH_NINTENDO_GAMECUBE_L,
    GLYPH_NINTENDO_GAMECUBE_R,
    GLYPH_NINTENDO_GAMECUBE_Z,

    GLYPH_TOTAL
}
ButtonGlyphKey;

static char glyph[GLYPH_TOTAL][5];

static bool keyboard_is_active = true;

void BUTTONGLYPHS_init(void)
{
    /* Set glyph array to strings for all the button glyph codepoints (U+EBxx) */
    for (int i = 0; i < GLYPH_TOTAL; i++)
    {
        strlcpy(glyph[i], UTF8_encode(0xEB00+i).bytes, sizeof(glyph[i]));
    }
}

bool BUTTONGLYPHS_keyboard_is_available(void)
{
    /* Returns true if it makes sense to show button hints that are only available
     * on keyboards (like press M to mute), false if we're on a console. */

    return false;
}

bool BUTTONGLYPHS_keyboard_is_active(void)
{
    /* Returns true if, not only do we have a keyboard available, but it's also the
     * active input method. (So, show keyboard keys, if false, show controller glyphs) */
    return keyboard_is_active;
}

void BUTTONGLYPHS_keyboard_set_active(bool active)
{
    keyboard_is_active = active;
}

const char* BUTTONGLYPHS_get_wasd_text(void)
{
    /* Returns the string to use in Welcome Aboard */
    if (BUTTONGLYPHS_keyboard_is_active())
    {
        return loc::gettext("Press arrow keys or WASD to move");
    }
    return loc::gettext("Press left/right to move");
}

static const char* glyph_for_button(
    const psp_key button
) {
    switch (button)
    {
        case PSP_CTRL_CIRCLE:   return glyph[GLYPH_PLAYSTATION_CIRCLE];
        case PSP_CTRL_TRIANGLE: return glyph[GLYPH_PLAYSTATION_TRIANGLE];
        case PSP_CTRL_SQUARE:   return glyph[GLYPH_PLAYSTATION_SQUARE];
        case PSP_CTRL_CROSS:    return glyph[GLYPH_PLAYSTATION_CROSS];
        case PSP_CTRL_START:    return glyph[GLYPH_PLAYSTATION_START];
        case PSP_CTRL_LTRIGGER: return glyph[GLYPH_NINTENDO_L];
        case PSP_CTRL_RTRIGGER: return glyph[GLYPH_NINTENDO_R];
        default:                return glyph[GLYPH_UNKNOWN];
    }
}

const char* BUTTONGLYPHS_get_button(const ActionSet actionset, const Action action, int binding)
{
    /* Given a specific action (like INTERACT in-game),
     * return either a (localized) keyboard key string like "ENTER" or "E",
     * or a controller button glyph from the table above like glyph[GLYPH_XBOX_Y],
     * to fill into strings like "Press {button} to activate terminal".
     *
     * Normally, set binding = -1. This will return the best keyboard key OR controller glyph.
     *
     * If binding >= 0, select a specific CONTROLLER binding glyph,
     * or NULL if the index is higher than the max binding index. */

    bool show_controller = binding >= 0 || !BUTTONGLYPHS_keyboard_is_active();
    if (binding < 0)
    {
        binding = 0;
    }

    switch (actionset)
    {
    case ActionSet_Menu:
        switch (action.Menu)
        {
        case Action_Menu_Accept:
            if (show_controller)
            {
                return glyph_for_button(game.controllerButton_flip);
            }
            return loc::gettext("ACTION");
        }
        break;
    case ActionSet_InGame:
        switch (action.InGame)
        {
        case Action_InGame_ACTION:
            if (show_controller)
            {
                return glyph_for_button(game.controllerButton_flip);
            }
            return loc::gettext("ACTION");

        case Action_InGame_Interact:
            if (show_controller)
            {
                /* FIXME: this really does depend on the Enter/E speedrunner option...
                 * This is messy, but let's not show the wrong thing here... */
                if (game.separate_interact)
                {
                    return glyph_for_button(game.controllerButton_interact);
                }
                return glyph_for_button(game.controllerButton_map);
            }
            if (game.separate_interact)
            {
                return "E";
            }
            return loc::gettext("ENTER");

        case Action_InGame_Map:
            if (show_controller)
            {
                return glyph_for_button(game.controllerButton_map);
            }
            return loc::gettext("ENTER");

        case Action_InGame_Esc:
            if (show_controller)
            {
                return glyph_for_button(game.controllerButton_esc);
            }
            return loc::gettext("ESC");

        case Action_InGame_LTrigger:
            if (show_controller)
            {
                return glyph_for_button(PSP_CTRL_LTRIGGER);
            }
            return ",";
        
        case Action_InGame_Square:
            if (show_controller)
            {
                return glyph_for_button(PSP_CTRL_SQUARE);
            }
            return "";

        case Action_InGame_Restart:
            if (show_controller)
            {
                return glyph_for_button(game.controllerButton_restart);
            }
            return "R";
        }
        break;
    }

    assert(0 && "Trying to get label/glyph for unknown action!");
    return glyph[GLYPH_UNKNOWN];
}

} // extern "C"
