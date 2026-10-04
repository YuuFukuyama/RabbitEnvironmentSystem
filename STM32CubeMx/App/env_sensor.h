#ifndef ENV_SENSOR_H
#define ENV_SENSOR_H

/* 温湿度取得。実装は CMake でダミーか本番を選ぶ。 */

#include "stm32f1xx_hal.h"
#include "env_types.h"

/* 成功時は ok を 1 にして HAL_OK。失敗時は ok を 0 にして HAL_ERROR。 */
HAL_StatusTypeDef EnvSensor_Read(EnvReading *reading);

#endif
