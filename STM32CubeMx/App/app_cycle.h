#ifndef APP_CYCLE_H
#define APP_CYCLE_H

/* 取得から通知の引き渡しまでを 1 周期分行う。 */

#include <stdint.h>
#include "stm32f1xx_hal.h"

/* 取得、再取得、判定、LCD 更新、通知の引き渡しを 1 回行う。 */
void App_MeasureOnce(I2C_HandleTypeDef *hi2c, uint32_t now_sec);

#endif
