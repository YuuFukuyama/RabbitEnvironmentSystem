#ifndef BOARD_LED_H
#define BOARD_LED_H

#include <stdint.h>

/* 基板 LED（PA15）。on が 0 なら消灯、1 なら点灯。 */
void BoardLed_SetAlert(uint8_t on);

#endif
