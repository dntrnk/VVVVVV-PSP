#include <SDL.h>
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

// static int oldscrollamount = 0;
// static int scrollamount = 0;
// static bool isscrolling = 0;

// void UpdateFilter(void)
// {
//     if (rand() % 4000 < 8)
//     {
//         isscrolling = true;
//     }

//     oldscrollamount = scrollamount;
//     if(isscrolling == true)
//     {
//         scrollamount += 20;
//         if(scrollamount > 240)
//         {
//             scrollamount = 0;
//             oldscrollamount = 0;
//             isscrolling = false;
//         }
//     }
// }

// void ApplyFilter(SDL_Surface** src, SDL_Surface** dest)
// {
//     // if (src == NULL || dest == NULL)
//     // {
//     //     assert(0 && "NULL src or dest!");
//     //     return;
//     // }

//     // if (*src == NULL)
//     // {
//     //     *src = SDL_CreateRGBSurface(0, SCREEN_WIDTH_PIXELS, SCREEN_HEIGHT_PIXELS, 32, 0, 0, 0, 0);
//     // }
//     // if (*dest == NULL)
//     // {
//     //     *dest = SDL_CreateRGBSurface(0, SCREEN_WIDTH_PIXELS, SCREEN_HEIGHT_PIXELS, 32, 0, 0, 0, 0);
//     // }
//     // if (*src == NULL || *dest == NULL)
//     // {
//     //     WHINE_ONCE_ARGS(("Could not create temporary surfaces: %s", SDL_GetError()));
//     //     return;
//     // }

//     // const int result = SDL_RenderReadPixels(gameScreen.m_renderer, NULL, 0, (*src)->pixels, (*src)->pitch);
//     // if (result != 0)
//     // {
//     //     SDL_FreeSurface(*src);
//     //     WHINE_ONCE_ARGS(("Could not read pixels from renderer: %s", SDL_GetError()));
//     //     return;
//     // }

//     // const int red_offset = rand() % 4;

//     // for (int x = 0; x < (*src)->w; x++)
//     // {
//     //     for (int y = 0; y < (*src)->h; y++)
//     //     {
//     //         const int sampley = (y + (int) graphics.lerp(oldscrollamount, scrollamount)) % 240;

//     //         const g2dColor pixel = ReadPixel(*src, x, sampley);

//     //         Uint8 green = G2D_GET_G(pixel);
//     //         Uint8 blue = G2D_GET_B(pixel);

//     //         const g2dColor pixel_offset = ReadPixel(*src, std::min(x + red_offset, 319), sampley);
//     //         Uint8 red = G2D_GET_R(pixel_offset);

//     //         double mult;
//     //         int tmp; /* needed to avoid char overflow */
//     //         if (isscrolling && sampley > 220 && ((rand() % 10) < 4))
//     //         {
//     //             mult = 0.6;
//     //         }
//     //         else
//     //         {
//     //             mult = 0.2;
//     //         }

//     //         tmp = red + fRandom() * mult * 254;
//     //         red = std::min(tmp, 255);
//     //         tmp = green + fRandom() * mult * 254;
//     //         green = std::min(tmp, 255);
//     //         tmp = blue + fRandom() * mult * 254;
//     //         blue = std::min(tmp, 255);

//     //         if (y % 2 == 0)
//     //         {
//     //             red = (Uint8) (red / 1.2f);
//     //             green = (Uint8) (green / 1.2f);
//     //             blue = (Uint8) (blue / 1.2f);
//     //         }

//     //         int distX = (int) ((std::abs(160.0f - x) / 160.0f) * 16);
//     //         int distY = (int) ((std::abs(120.0f - y) / 120.0f) * 32);

//     //         red = std::max(red - (distX + distY), 0);
//     //         green = std::max(green - (distX + distY), 0);
//     //         blue = std::max(blue - (distX + distY), 0);

//     //         const g2dColor color = G2D_RGBA(red, green, blue, G2D_GET_A(pixel));
//     //         DrawPixel(*dest, x, y, color);
//     //     }
//     // }

//     // SDL_UpdateTexture(graphics.gameTexture, NULL, (*dest)->pixels, (*dest)->pitch);
// }
