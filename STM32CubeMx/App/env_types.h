#ifndef ENV_TYPES_H
#define ENV_TYPES_H

/* 温湿度判定で共有する危険度、個別状態、測定値。 */

#include <stdint.h>

/* 総合危険度。値が大きいほど危険。 */
typedef enum
{
  ENV_RISK_NORMAL = 0,
  ENV_RISK_WARNING = 1,
  ENV_RISK_DANGER = 2,
  ENV_RISK_CRITICAL = 3
} EnvRisk;

/* 温度の個別状態。 */
typedef enum
{
  TEMP_FRZN = 0,
  TEMP_COLD,
  TEMP_BEST,
  TEMP_HIGH,
  TEMP_CRIT
} TempState;

/* 湿度の個別状態。 */
typedef enum
{
  HUM_DRY = 0,
  HUM_LOW,
  HUM_BEST,
  HUM_HIGH,
  HUM_WET
} HumState;

/* 環境危険度とは別に持つ稼働状態。 */
typedef enum
{
  SYS_NORMAL = 0,
  SYS_SENSOR_ERROR,
  SYS_WIFI_ERROR,
  SYS_SYSTEM_ERROR
} SystemStatus;

/* 温度・湿度は 0.1 単位。22.5℃ は 225。 */
typedef struct
{
  int16_t temp_deci_c;
  int16_t hum_deci_rh;
  uint8_t ok;
} EnvReading;

/* LCD が1周期分を描くための状態。 */
typedef struct
{
  uint8_t sensor_error;
  uint8_t has_value;
  uint8_t show_minutes;
  uint8_t minutes;
  int16_t temp_deci_c;
  int16_t hum_deci_rh;
  EnvRisk risk;
} EnvView;

#endif
