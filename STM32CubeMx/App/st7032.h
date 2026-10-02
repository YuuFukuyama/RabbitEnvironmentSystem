#ifndef ST7032_H
#define ST7032_H

#include "stm32f1xx_hal.h"

HAL_StatusTypeDef ST7032_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef ST7032_WriteString(I2C_HandleTypeDef *hi2c, const char *text);

#endif
