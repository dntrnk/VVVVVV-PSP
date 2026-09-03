#ifdef __cplusplus
extern "C" {
#endif

#ifndef CONTROLS_H
#define CONTROLS_H

#include <stdbool.h>
#include <string.h>

#include <pspctrl.h>

void controls_init(void);
void controls_read(void);
bool controls_pressed(const unsigned int button);
bool controls_held(const unsigned int button);
bool controls_released(const unsigned int button);
int controls_AnalogX(void);
int controls_AnalogY(void);

#endif // CONTROLS_H

#ifdef __cplusplus
}
#endif