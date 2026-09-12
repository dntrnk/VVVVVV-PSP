#include "BlockV.h"

#include "Script.h"
#include "Font.h"

blockclass::blockclass(void)
{
    clear();
}

void blockclass::clear(void)
{
    type = 0;
    trigger = 0;

    xp = 0;
    yp = 0;
    wp = 0;
    hp = 0;
    rect_x = xp;
    rect_y = yp;
    rect_w = wp;
    rect_h = hp;

    r = 0;
    g = 0;
    b = 0;

    activity_y = 0;

    /* std::strings get initialized automatically, but this is
     * in case this function gets called again after construction */
    script.clear();
    prompt.clear();

    gettext = true;
}

void blockclass::rectset(const int xi, const int yi, const int wi, const int hi)
{
    rect_x = xi;
    rect_y = yi;
    rect_w = wi;
    rect_h = hi;
}

void blockclass::setblockcolour(const char* col)
{
    bool exists = ::script.textbox_colours.count(col) != 0;

    r = G2D_GET_R(::script.textbox_colours[exists ? col : "gray"]);
    g = G2D_GET_G(::script.textbox_colours[exists ? col : "gray"]);
    b = G2D_GET_B(::script.textbox_colours[exists ? col : "gray"]);
}
