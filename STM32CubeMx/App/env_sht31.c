/* GY-SHT31-D から温度と湿度を 1 周期分読む。 */

#include "env_sensor.h"

/* 7bit アドレス 0x44。HAL には左へ 1bit 寄せた値を渡す。 */
#define SHT31_I2C_ADDR_8BIT    (0x44U << 1)
#define SHT31_I2C_TIMEOUT_MS   100U
#define SHT31_MEASURE_WAIT_MS  15U
#define SHT31_CMD_GAP_MS       1U
#define SHT31_RESET_WAIT_MS    2U

/* 単発、繰り返し性 High、ストレッチ無し。 */
#define SHT31_CMD_MEASURE_HI   0x24U
#define SHT31_CMD_MEASURE_LO   0x00U
/* ソフトリセット。次の命令まで最大 1.5 ms。 */
#define SHT31_CMD_RESET_HI     0x30U
#define SHT31_CMD_RESET_LO     0xA2U

/* I2C1。PB6 が SCL、PB7 が SDA。 */
#define SHT31_SCL_PIN          GPIO_PIN_6
#define SHT31_SDA_PIN          GPIO_PIN_7

/* 1 回の測定の結果。NACK と CRC 不一致は次周期でソフトリセットする。 */
typedef enum
{
  SHT_OK = 0,
  SHT_NACK_OR_CRC,
  SHT_BUS
} ShtResult;

/* 前周期が NACK または CRC 不一致で終わったとき 1。 */
static uint8_t reset_before_measure;

/* CRC-8。多項式 0x31、初期値 0xFF。対象は直前の 2 バイト。 */
static uint8_t sht31_crc(uint8_t hi, uint8_t lo)
{
  uint8_t crc;
  uint8_t index;
  uint8_t bit;
  uint8_t data[2];

  data[0] = hi;
  data[1] = lo;
  crc = 0xFFU;
  for (index = 0U; index < 2U; index++)
  {
    crc ^= data[index];
    for (bit = 0U; bit < 8U; bit++)
    {
      if ((crc & 0x80U) != 0U)
      {
        crc = (uint8_t)((uint8_t)(crc << 1) ^ 0x31U);
      }
      else
      {
        crc = (uint8_t)(crc << 1);
      }
    }
  }
  return crc;
}

/* 生値を 0.1 ℃へ変換する。 */
static int16_t sht31_temp_deci(uint16_t raw)
{
  uint32_t scaled;

  scaled = ((1750UL * (uint32_t)raw) + 32767UL) / 65535UL;
  return (int16_t)((int32_t)scaled - 450);
}

/* 生値を 0.1 %RH へ変換する。 */
static int16_t sht31_hum_deci(uint16_t raw)
{
  uint32_t scaled;

  scaled = ((1000UL * (uint32_t)raw) + 32767UL) / 65535UL;
  return (int16_t)scaled;
}

/* SCL のパルス幅を空ける。 */
static void sht31_scl_hold(void)
{
  volatile uint16_t count;

  for (count = 0U; count < 200U; count++)
  {
  }
}

/* タイムアウト、ビジー、バスエラーのあと、バスを 1 回戻す。 */
static void sht31_recover_bus(I2C_HandleTypeDef *hi2c)
{
  GPIO_InitTypeDef gpio;
  uint8_t pulse;

  SET_BIT(hi2c->Instance->CR1, I2C_CR1_STOP);
  if (__HAL_I2C_GET_FLAG(hi2c, I2C_FLAG_BUSY) == SET)
  {
    (void)HAL_I2C_Init(hi2c);
  }

  if (HAL_GPIO_ReadPin(GPIOB, SHT31_SDA_PIN) == GPIO_PIN_RESET)
  {
    __HAL_I2C_DISABLE(hi2c);
    GPIOB->BSRR = (uint32_t)SHT31_SCL_PIN | (uint32_t)SHT31_SDA_PIN;
    gpio.Pin = SHT31_SCL_PIN | SHT31_SDA_PIN;
    gpio.Mode = GPIO_MODE_OUTPUT_OD;
    gpio.Pull = GPIO_NOPULL;
    gpio.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &gpio);
    HAL_GPIO_WritePin(GPIOB, SHT31_SCL_PIN | SHT31_SDA_PIN, GPIO_PIN_SET);

    for (pulse = 0U; pulse < 9U; pulse++)
    {
      HAL_GPIO_WritePin(GPIOB, SHT31_SCL_PIN, GPIO_PIN_RESET);
      sht31_scl_hold();
      HAL_GPIO_WritePin(GPIOB, SHT31_SCL_PIN, GPIO_PIN_SET);
      sht31_scl_hold();
    }

    HAL_GPIO_WritePin(GPIOB, SHT31_SCL_PIN, GPIO_PIN_RESET);
    sht31_scl_hold();
    HAL_GPIO_WritePin(GPIOB, SHT31_SDA_PIN, GPIO_PIN_RESET);
    sht31_scl_hold();
    HAL_GPIO_WritePin(GPIOB, SHT31_SCL_PIN, GPIO_PIN_SET);
    sht31_scl_hold();
    HAL_GPIO_WritePin(GPIOB, SHT31_SDA_PIN, GPIO_PIN_SET);
    sht31_scl_hold();

    hi2c->State = HAL_I2C_STATE_RESET;
    (void)HAL_I2C_Init(hi2c);
  }

  hi2c->ErrorCode = HAL_I2C_ERROR_NONE;
  hi2c->State = HAL_I2C_STATE_READY;
  hi2c->Mode = HAL_I2C_MODE_NONE;
  __HAL_UNLOCK(hi2c);
}

/* NACK だけはデバイスの失敗。それ以外はバスを戻す。 */
static uint8_t sht31_bus_fault(I2C_HandleTypeDef *hi2c, HAL_StatusTypeDef status)
{
  uint32_t err;

  if (status == HAL_OK)
  {
    return 0U;
  }

  err = hi2c->ErrorCode;
  if ((status != HAL_BUSY) && ((err & HAL_I2C_ERROR_AF) != 0U) && ((err & ~HAL_I2C_ERROR_AF) == 0U))
  {
    return 0U;
  }
  return 1U;
}

/* 1 転送。失敗時は結果だけ返し、NACK の読み出しはやり直さない。 */
static ShtResult sht31_xfer(I2C_HandleTypeDef *hi2c, uint8_t *data, uint16_t size, uint8_t read)
{
  HAL_StatusTypeDef status;

  if (read != 0U)
  {
    status = HAL_I2C_Master_Receive(hi2c, SHT31_I2C_ADDR_8BIT, data, size, SHT31_I2C_TIMEOUT_MS);
  }
  else
  {
    status = HAL_I2C_Master_Transmit(hi2c, SHT31_I2C_ADDR_8BIT, data, size, SHT31_I2C_TIMEOUT_MS);
  }
  if (status == HAL_OK)
  {
    return SHT_OK;
  }
  if (sht31_bus_fault(hi2c, status) != 0U)
  {
    sht31_recover_bus(hi2c);
    return SHT_BUS;
  }
  return SHT_NACK_OR_CRC;
}

/* バスが空いているときのソフトリセット。 */
static void sht31_soft_reset(I2C_HandleTypeDef *hi2c)
{
  uint8_t cmd[2];
  ShtResult result;

  cmd[0] = SHT31_CMD_RESET_HI;
  cmd[1] = SHT31_CMD_RESET_LO;
  result = sht31_xfer(hi2c, cmd, 2U, 0U);
  if (result == SHT_OK)
  {
    HAL_Delay(SHT31_RESET_WAIT_MS);
  }
  else
  {
    HAL_Delay(SHT31_CMD_GAP_MS);
  }
}

/* 命令、測定待ち、6 バイト読み、CRC。成功時だけ測定値を書く。 */
static ShtResult sht31_measure_once(I2C_HandleTypeDef *hi2c, EnvReading *reading)
{
  uint8_t cmd[2];
  uint8_t raw[6];
  ShtResult result;
  uint16_t temp_raw;
  uint16_t hum_raw;

  cmd[0] = SHT31_CMD_MEASURE_HI;
  cmd[1] = SHT31_CMD_MEASURE_LO;
  result = sht31_xfer(hi2c, cmd, 2U, 0U);
  if (result != SHT_OK)
  {
    return result;
  }

  HAL_Delay(SHT31_MEASURE_WAIT_MS);

  result = sht31_xfer(hi2c, raw, 6U, 1U);
  if (result != SHT_OK)
  {
    return result;
  }
  if ((sht31_crc(raw[0], raw[1]) != raw[2]) || (sht31_crc(raw[3], raw[4]) != raw[5]))
  {
    return SHT_NACK_OR_CRC;
  }

  temp_raw = (uint16_t)(((uint16_t)raw[0] << 8) | raw[1]);
  hum_raw = (uint16_t)(((uint16_t)raw[3] << 8) | raw[4]);
  reading->temp_deci_c = sht31_temp_deci(temp_raw);
  reading->hum_deci_rh = sht31_hum_deci(hum_raw);
  reading->ok = 1U;
  return SHT_OK;
}

HAL_StatusTypeDef EnvSensor_Read(I2C_HandleTypeDef *hi2c, EnvReading *reading)
{
  ShtResult result;

  if ((hi2c == NULL) || (reading == NULL))
  {
    return HAL_ERROR;
  }

  reading->temp_deci_c = 0;
  reading->hum_deci_rh = 0;
  reading->ok = 0U;

  if (reset_before_measure != 0U)
  {
    reset_before_measure = 0U;
    sht31_soft_reset(hi2c);
  }

  result = sht31_measure_once(hi2c, reading);
  if (result != SHT_OK)
  {
    reading->ok = 0U;
    HAL_Delay(SHT31_CMD_GAP_MS);
    result = sht31_measure_once(hi2c, reading);
  }
  if (result == SHT_OK)
  {
    return HAL_OK;
  }

  reading->temp_deci_c = 0;
  reading->hum_deci_rh = 0;
  reading->ok = 0U;
  if (result == SHT_NACK_OR_CRC)
  {
    reset_before_measure = 1U;
  }
  return HAL_ERROR;
}
