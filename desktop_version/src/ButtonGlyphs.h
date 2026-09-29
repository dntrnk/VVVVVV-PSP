#ifndef BUTTONGLYPHS_H
#define BUTTONGLYPHS_H

#ifdef __cplusplus
#include <cstddef>
#else
#include <stdint.h>
#endif

#include <stdbool.h>

#include "ActionSets.h"

#ifdef __cplusplus
extern "C"
{
#endif

void BUTTONGLYPHS_init(void);

bool BUTTONGLYPHS_keyboard_is_available(void);
bool BUTTONGLYPHS_keyboard_is_active(void);
void BUTTONGLYPHS_keyboard_set_active(bool active);

const char* BUTTONGLYPHS_get_wasd_text(void);
const char* BUTTONGLYPHS_get_button(ActionSet actionset, Action action, int binding);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // BUTTONGLYPHS_H
