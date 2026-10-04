#ifndef ST7032_H
#define ST7032_H

#include "stm32f1xx_hal.h"

HAL_StatusTypeDef ST7032_Init(I2C_HandleTypeDef *hi2c);
HAL_StatusTypeDef ST7032_SetCursor(I2C_HandleTypeDef *hi2c, uint8_t address);
HAL_StatusTypeDef ST7032_WriteString(I2C_HandleTypeDef *hi2c, const char *text);

/* アイコン S56 を点ける。on が 0 のときは消す。 */
HAL_StatusTypeDef ST7032_SetIconS56(I2C_HandleTypeDef *hi2c, uint8_t on);

#endif
