#ifndef LCD_STATUS_H
#define LCD_STATUS_H

/* 環境状態を 16 桁×2 行の LCD に書く。 */

#include "stm32f1xx_hal.h"
#include "env_types.h"

/* 16 桁×2 行へ環境状態を書く。 */
HAL_StatusTypeDef LcdStatus_Show(I2C_HandleTypeDef *hi2c, const EnvView *view);

/* Wi-Fi 接続中はアンテナ S1 を点ける。connected が 0 のときは消す。 */
HAL_StatusTypeDef LcdStatus_ShowWifi(I2C_HandleTypeDef *hi2c, uint8_t connected);

#endif
