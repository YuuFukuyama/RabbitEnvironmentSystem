#ifndef ENV_JUDGE_H
#define ENV_JUDGE_H

/* 測定 1 回分の危険度更新と、送信結果の稼働状態への反映。 */

#include <stdint.h>
#include "env_types.h"
#include "wifi_link.h"

/* 1 回の測定結果で危険度・通知・表示用の状態を更新する。 */
void EnvJudge_Apply(const EnvReading *reading, uint32_t now_sec,
                    EnvView *view, WifiNote *notes, uint8_t *note_count,
                    uint8_t note_cap);

/* 起動時に誰か定期通知が ON なら 1。未取得と OFF は 0。 */
void EnvJudge_SetNotifyMode(uint8_t on);

/* 暦を書いたあと、判定に使っている秒を同じだけずらす。 */
void EnvJudge_AcceptClock(uint32_t old_sec, uint32_t new_sec);

/* この周期の測定を中継へ出す内容。 */
typedef struct
{
  uint8_t notify;       /* 1 なら異常・復帰・システム異常として送る。 */
  uint8_t periodic;     /* 1 なら定期通知の周期。notify が 1 のときはサーバが定期通知を重ねない。 */
  EnvRisk risk;         /* 全体の危険度。 */
  TempState temp_state; /* 温度の状態。 */
  HumState hum_state;   /* 湿度の状態。 */
  int16_t temp_deci_c;  /* 温度。0.1℃。 */
  int16_t hum_deci_rh;  /* 湿度。0.1%。 */
} EnvReport;

/* 直近の判定から、中継へ出す 1 件を作る。 */
void EnvJudge_CopyReport(const WifiNote *notes, uint8_t note_count, EnvReport *report);

/* 送信結果を稼働状態へ反映する。センサー異常中は上書きしない。 */
void EnvJudge_NoteWifi(uint8_t failed);

#endif
