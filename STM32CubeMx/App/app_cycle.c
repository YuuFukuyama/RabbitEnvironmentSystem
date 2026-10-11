/* 取得、判定、LCD 更新、AT の確認、通知の引き渡しを 1 回行う。 */

#include "app_cycle.h"
#include "at_cmd.h"
#include "env_judge.h"
#include "env_sensor.h"
#include "lcd_status.h"

#define APP_NOTE_CAP 3U

void App_MeasureOnce(I2C_HandleTypeDef *hi2c, uint32_t now_sec)
{
  EnvReading reading;
  EnvView view;
  WifiNote notes[APP_NOTE_CAP];
  uint8_t note_count;
  uint8_t index;
  uint8_t wifi_failed;

  reading.temp_deci_c = 0;
  reading.hum_deci_rh = 0;
  reading.ok = 0U;
  note_count = 0U;
  wifi_failed = 0U;

  /* 通信失敗は 1 回だけ取り直す。ダミーの失敗行もこの 2 回で 1 件進む。 */
  if (EnvSensor_Read(&reading) != HAL_OK)
  {
    (void)EnvSensor_Read(&reading);
  }

  EnvJudge_Apply(&reading, now_sec, &view, notes, &note_count, APP_NOTE_CAP);
  (void)LcdStatus_Show(hi2c, &view);
  /* AT と Wi-Fi の確認。接続中だけアンテナを点ける。失敗しても測定は続ける。 */
  if (At_Probe() == HAL_OK)
  {
    (void)LcdStatus_ShowWifi(hi2c, 1U);
  }
  else
  {
    (void)LcdStatus_ShowWifi(hi2c, 0U);
  }

  for (index = 0U; index < note_count; index++)
  {
    if (Wifi_Publish(&notes[index]) != HAL_OK)
    {
      wifi_failed = 1U;
    }
  }
  if (note_count > 0U)
  {
    EnvJudge_NoteWifi(wifi_failed);
  }
}
