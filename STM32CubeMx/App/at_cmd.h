#ifndef AT_CMD_H
#define AT_CMD_H

/* ESP32-C3 の AT ポートへ、測定周期に 1 回だけ問い合わせる。 */

#include "stm32f1xx_hal.h"

/* AT、AT+GMR、未接続なら Wi-Fi 参加を行う。失敗しても周期は止めない。 */
HAL_StatusTypeDef At_Probe(void);

#endif
