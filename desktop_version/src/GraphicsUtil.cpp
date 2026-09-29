#include <stddef.h>
#include <stdlib.h>
#include <cassert>

#include "Alloc.h"
#include "Constants.h"
#include "Graphics.h"
#include "Maths.h"
#include "Screen.h"
#include "UtilityClass.h"
#include "Vlogging.h"

void setRect( VVV_Rect& _r, int x, int y, int w, int h )
{
    _r.x = x;
    _r.y = y;
    _r.w = w;
    _r.h = h;
}
