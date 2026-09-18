/*
 * mod_payload.c
 *
 *  Created on: Sep 17, 2026
 *      Author: aliawolken
 *
 *      Hard switch implementation for now. PA13 is active low and cuts power line.
 *      HIGH is default and = payload ON, LOW = payload OFF
 */

#include "mod_payload.h"
#include "em_gpio.h"
#include "em_cmu.h"
#include "sl_uartdrv_instances.h"
#include <stdio.h>
#include "os.h"
#include "rtos_err.h"

#define PAYLOAD_OUTPUT_PORT  gpioPortA
#define PAYLOAD_OUTPUT_PIN   13

static payload_state_t payload_commanded = PAYLOAD_STATE_UNKNOWN;

payload_state_t payload_get_commanded(void) { return payload_commanded; }

bool payload_init(void)
{
  CMU_ClockEnable(cmuClock_GPIO, true);
  GPIO_PinModeSet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN, gpioModePushPull, 1); // HIGH = instrument ON
  payload_commanded = PAYLOAD_STATE_MEASURING;

  // TEMP bench test: send the line 10 times, 0.5 s apart (delays boot ~5 s)
  RTOS_ERR err;
  static uint8_t test_msg[] = "UUUU payload UART0 test\r\n";
  for (int i = 0; i < 10; i++) {
      Ecode_t ec = UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, test_msg, sizeof(test_msg) - 1);
      printf("payload UART0 test %d sent, ecode=%lu\r\n", i, (unsigned long)ec);
      OSTimeDly(500, OS_OPT_TIME_DLY, &err);
  }

  // TODO software switch: DONT assume state of payload.
  // Serial payload runs through MCU reset so on startup payload may be in measurement mode.
  // Use GETSTATE to query and set payload commanded based on the reply.
  // Set PAYLOAD_STATE_UNKNOWN if no reply.

  return true;
}

// sets the payload ON or in measurement mode
bool payload_ctrl_meas(void)
{
  GPIO_PinOutSet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN); // HIGH = instrument ON
  payload_commanded = PAYLOAD_STATE_MEASURING;
  return true;
}

// sets payload OFF or out of measurement mode
bool payload_ctrl_sleep(void)
{
  GPIO_PinOutClear(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN); // LOW = instrument OFF
  payload_commanded = PAYLOAD_STATE_SLEEP;
  return true;
}

// checks status of payload
payload_state_t payload_status(void)
{
  // commanded, not confirmed: the output register says what we drove,
  // not what the instrument did with it
  if (GPIO_PinOutGet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN)) {
      return PAYLOAD_STATE_MEASURING;
  } else {
      return PAYLOAD_STATE_SLEEP;
  }
}

