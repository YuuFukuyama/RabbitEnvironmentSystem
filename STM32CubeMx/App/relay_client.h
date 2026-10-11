#ifndef RELAY_CLIENT_H
#define RELAY_CLIENT_H

/* 中継への操作取得と測定送信。ダミー取得のときも同じ手順で送る。 */

#include <stdint.h>
#include "stm32f1xx_hal.h"
#include "wifi_link.h"

/* ホストとトークンが両方あれば 1。 */
uint8_t Relay_Enabled(void);

/* 未取得なら起動時の操作取得を行う。時計を更新したら 1 とし、*synced_sec にカウンタを入れる。 */
uint8_t Relay_TakeBoot(uint32_t *synced_sec);

/* 直近の判定を 1 件送る。取得がダミーでも送る。 */
HAL_StatusTypeDef Relay_Post(const WifiNote *notes, uint8_t note_count);

#endif
