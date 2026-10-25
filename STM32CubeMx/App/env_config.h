#ifndef ENV_CONFIG_H
#define ENV_CONFIG_H

/* 異常判定の閾値と、測定・通知の周期。 */

#include <stdint.h>

/* 閾値は 0.1 単位。境界は詳細設計の「以下」「未満」に合わせる。 */
#define TEMP_LOW_CRITICAL_DECI   100
#define TEMP_LOW_WARNING_DECI    180
#define TEMP_LOW_CLEAR_DECI      190
#define TEMP_HIGH_WARNING_DECI   260
#define TEMP_HIGH_CLEAR_DECI     260
#define TEMP_HIGH_CRITICAL_DECI  280

#define HUM_LOW_CRITICAL_DECI    150
#define HUM_LOW_WARNING_DECI     400
#define HUM_LOW_CLEAR_DECI       410
#define HUM_HIGH_WARNING_DECI    600
#define HUM_HIGH_CLEAR_DECI      590
#define HUM_HIGH_CRITICAL_DECI   700

#define SPEC_MEASURE_INTERVAL_SEC 600U

/* 測定間隔。CMake の ENV_FAST_MEASURE で上書き可。 */
#ifndef MEASURE_INTERVAL_SEC
#define MEASURE_INTERVAL_SEC 600U
#endif

/* 詳細設計の分数を、10 分周期の測定回数と同じになる秒数へ換算する。 */
#define ENV_SCALE_SEC(spec_sec) \
  (((uint32_t)(spec_sec) * MEASURE_INTERVAL_SEC) / SPEC_MEASURE_INTERVAL_SEC)

#define WARNING_CONFIRM_COUNT    3U
#define NORMAL_RESET_COUNT       4U
#define WARNING_ESCALATION_SEC   ENV_SCALE_SEC(60U * 60U)
#define WARNING_REPEAT_SEC       ENV_SCALE_SEC(30U * 60U)
#define DANGER_NOTIFY_SEC        ENV_SCALE_SEC(20U * 60U)
#define DANGER_ESCALATION_SEC    ENV_SCALE_SEC(30U * 60U)
/* 生存確認は測定周期の 6 回ごと。載せるかは起動時の notifyMode が ON のときだけ。 */
#define HEARTBEAT_SEC            (6U * MEASURE_INTERVAL_SEC)
#define HEARTBEAT_ENABLE         1

#endif
