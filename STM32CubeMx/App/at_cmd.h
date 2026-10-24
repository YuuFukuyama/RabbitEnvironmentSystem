#ifndef AT_CMD_H
#define AT_CMD_H

/* ESP32-C3 の AT ポートへ、測定周期に 1 回だけ問い合わせる。 */

#include "stm32f1xx_hal.h"

/* AT、AT+GMR、未接続なら Wi-Fi 参加を行う。失敗しても周期は止めない。 */
HAL_StatusTypeDef At_Probe(void);

/* 再接続を止めてから AP を切る。失敗しても周期は止めない。 */
HAL_StatusTypeDef At_Disconnect(void);

/* 以降の HTTP に Authorization と Content-Type を付ける。 */
HAL_StatusTypeDef At_HttpAuth(const char *token);

/* HTTPS GET を行い、応答を At_HttpResponse に残す。 */
HAL_StatusTypeDef At_HttpGet(const char *url);

/* 起動時 ops 用 GET。JSON が at_rx に載るまで待つ。 */
HAL_StatusTypeDef At_HttpGetBoot(const char *url);

/* HTTPS POST の本文を送り、応答を At_HttpResponse に残す。 */
HAL_StatusTypeDef At_HttpPost(const char *url, const char *body);

/* 直前の HTTP 応答。 */
const char *At_HttpResponse(void);

#endif
