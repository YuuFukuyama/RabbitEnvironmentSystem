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

/* 送信結果を稼働状態へ反映する。センサー異常中は上書きしない。 */
void EnvJudge_NoteWifi(uint8_t failed);

#endif
