#ifndef WIFI_LINK_H
#define WIFI_LINK_H

/* Wi-Fi 側へ渡す通知。電文の組み立てはここでは行わない。 */

#include "stm32f1xx_hal.h"
#include "env_types.h"

/* 通知の種別。 */
typedef enum
{
  WIFI_NOTE_ALERT = 0,
  WIFI_NOTE_REPEAT,
  WIFI_NOTE_RECOVERY,
  WIFI_NOTE_HEARTBEAT,
  WIFI_NOTE_SENSOR_ERROR
} WifiNoteKind;

/* 通知 1 件。温湿度は 0.1 単位。 */
typedef struct
{
  WifiNoteKind kind;
  EnvRisk risk;
  TempState temp_state;
  HumState hum_state;
  int16_t temp_deci_c;
  int16_t hum_deci_rh;
} WifiNote;

#if defined(WIFI_USE_DUMMY)
#define WIFI_DUMMY_LOG_LEN 4U
/* ダミーが残す直近の通知。新しいものから上書きする。 */
extern WifiNote wifi_dummy_log[WIFI_DUMMY_LOG_LEN];
/* wifi_dummy_log に入っている件数。 */
extern uint8_t wifi_dummy_log_count;
#endif

/* 通知を 1 件渡す。ダミーは記録して成功を返す。 */
HAL_StatusTypeDef Wifi_Publish(const WifiNote *note);

#endif
