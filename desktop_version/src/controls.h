#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONTROLS_H
#define CONTROLS_H

#include <stdbool.h>
#include <string.h>

#include <pspctrl.h>

typedef unsigned int psp_key;

void controls_init(void);
void controls_read(void);
bool controls_pressed(const psp_key button);
bool controls_held(const psp_key button);
bool controls_released(const psp_key button);
int controls_AnalogX(void);
int controls_AnalogY(void);

#endif // CONTROLS_H

#ifdef __cplusplus
}
#endif