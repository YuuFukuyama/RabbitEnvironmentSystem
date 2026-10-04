/* 温湿度のダミー取得。100 件を繰り返す。 */

#include "env_sensor.h"

#if !defined(ENV_SENSOR_USE_DUMMY)
#error ENV_SENSOR_USE_DUMMY を外すときは env_dummy.c を本番取得に差し替える
#endif

/* ダミー 1 件。fail が 1 のときは温湿度を使わない。 */
typedef struct
{
  int16_t temp_deci_c;
  int16_t hum_deci_rh;
  uint8_t fail;
} EnvDummyStep;

#define S(temp_deci, hum_deci) { (int16_t)(temp_deci), (int16_t)(hum_deci), 0U }
#define F() { 0, 0, 1U }
#define S2(temp_deci, hum_deci) S(temp_deci, hum_deci), S(temp_deci, hum_deci)
#define S3(temp_deci, hum_deci) S2(temp_deci, hum_deci), S(temp_deci, hum_deci)
#define S4(temp_deci, hum_deci) S2(temp_deci, hum_deci), S2(temp_deci, hum_deci)
#define S5(temp_deci, hum_deci) S4(temp_deci, hum_deci), S(temp_deci, hum_deci)
#define S7(temp_deci, hum_deci) S5(temp_deci, hum_deci), S2(temp_deci, hum_deci)
#define F3() F(), F(), F()

/* 測定ごとに 1 行進み、末尾の次は先頭に戻る。失敗行は再取得でも失敗させる。 */
static const EnvDummyStep env_dummy_steps[] =
{
  /* 正常値が一度もない取得失敗 */
  F3(),
  /* 正常 */
  S4(225, 500),
  /* 低温警告。18.5℃では解除せず、19.0℃で解除する */
  S2(170, 500),
  S(225, 500),
  S2(170, 500),
  S4(185, 500),
  S(190, 500),
  S4(225, 500),
  /* 高温警告。連続は昇格の 6 間隔未満 */
  S4(260, 500),
  S2(245, 500),
  S(235, 500),
  S5(225, 500),
  /* 乾燥警告。40.5% では解除せず、41% で解除する */
  S4(220, 300),
  S2(220, 405),
  S(220, 410),
  S5(225, 500),
  /* 高湿度警告。59.5% では解除せず、58% で解除する */
  S4(220, 650),
  S2(220, 595),
  S(220, 580),
  S5(225, 500),
  /* 両方 WARNING で DANGER。片側が戻っても維持する */
  S(265, 650),
  S2(265, 500),
  S3(225, 500),
  /* 高温・低温・乾燥・高湿の CRITICAL */
  S2(285, 500),
  S3(225, 500),
  S2(95, 500),
  S3(225, 500),
  S2(220, 100),
  S3(225, 500),
  S2(220, 750),
  S3(225, 500),
  /* 直前値がある状態の取得失敗 */
  F3(),
  /* 高温警告を続け、6 間隔で DANGER へ上げる */
  S7(260, 500),
  /* 複合 DANGER を続け、3 間隔で CRITICAL へ上げる */
  S4(265, 650),
  /* 正常へ戻す */
  S3(225, 500)
};

#define ENV_DUMMY_STEP_COUNT ((uint8_t)(sizeof(env_dummy_steps) / sizeof(env_dummy_steps[0])))

_Static_assert(sizeof(env_dummy_steps) / sizeof(env_dummy_steps[0]) == 100U,
               "dummy scenario length");

/* シナリオの現在位置。末尾の次は 0 に戻る。 */
static uint8_t step_index;
/* 失敗行の再取得待ち。1 のとき次の読み出しで行を進める。 */
static uint8_t retry_pending;

/* シナリオ位置を 1 つ進める。 */
static void advance_step(void)
{
  step_index++;
  if (step_index >= ENV_DUMMY_STEP_COUNT)
  {
    step_index = 0U;
  }
}

HAL_StatusTypeDef EnvSensor_Read(EnvReading *reading)
{
  const EnvDummyStep *step;

  if (reading == NULL)
  {
    return HAL_ERROR;
  }

  /* 失敗行の 1 回目は行を進めない。再取得で同じ行を失敗させ、そこで次へ進む。 */
  if (retry_pending != 0U)
  {
    retry_pending = 0U;
    reading->temp_deci_c = 0;
    reading->hum_deci_rh = 0;
    reading->ok = 0U;
    advance_step();
    return HAL_ERROR;
  }

  step = &env_dummy_steps[step_index];
  if (step->fail != 0U)
  {
    retry_pending = 1U;
    reading->temp_deci_c = 0;
    reading->hum_deci_rh = 0;
    reading->ok = 0U;
    return HAL_ERROR;
  }

  reading->temp_deci_c = step->temp_deci_c;
  reading->hum_deci_rh = step->hum_deci_rh;
  reading->ok = 1U;
  advance_step();
  return HAL_OK;
}
