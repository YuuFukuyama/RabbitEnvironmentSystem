/* 基板 LED2（PA15）の点灯制御。L で点灯。 */

#include "board_led.h"
#include "main.h"

void BoardLed_SetAlert(uint8_t on)
{
  GPIO_PinState level;

  level = (on != 0U) ? GPIO_PIN_RESET : GPIO_PIN_SET;
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, level);
}
