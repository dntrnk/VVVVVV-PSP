#include "VVVCompat.h"

bool VVV_PointInRect(const VVV_Point *p, const VVV_Rect *r)
{
    return ( (p->x >= r->x) && (p->x < (r->x + r->w)) &&
             (p->y >= r->y) && (p->y < (r->y + r->h)) ) ? true : false;
}