#ifndef ENV_SENSOR_H
#define ENV_SENSOR_H

/* 温湿度取得。既定は GY-SHT31-D。ENV_SENSOR_USE_DUMMY でダミーシナリオを選ぶ。 */

#include "stm32f1xx_hal.h"
#include "env_types.h"

/* 1 周期分を読む。成功時は ok を 1 にして HAL_OK。失敗時は ok を 0 にして HAL_ERROR。 */
HAL_StatusTypeDef EnvSensor_Read(I2C_HandleTypeDef *hi2c, EnvReading *reading);

#endif
