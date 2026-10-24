#ifndef LCD_STATUS_H
#define LCD_STATUS_H

/* 環境状態と日時を 16 桁×2 行の LCD に書く。 */

#include "stm32f1xx_hal.h"
#include "env_types.h"

/* 16 桁×2 行へ環境状態を書く。 */
HAL_StatusTypeDef LcdStatus_Show(I2C_HandleTypeDef *hi2c, const EnvView *view);

/* 接続できたときはアンテナ S1 を点ける。接続できなかったときは消す。 */
HAL_StatusTypeDef LcdStatus_ShowWifi(I2C_HandleTypeDef *hi2c, uint8_t connected);

#endif
