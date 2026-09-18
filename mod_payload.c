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

#define PAYLOAD_WAKE_MS        150  // after "@@@@@@" (from adcp.py)
#define PAYLOAD_BREAK_GAP_MS   400  // between the two "K1W%!Q" (from adcp.py)
#define PAYLOAD_REPLY_WAIT_MS  500  // TEMP: stands in for reading the reply

static uint8_t payload_wake_str[]      = "@@@@@@";
static uint8_t payload_break_str[]     = "K1W%!Q";
static uint8_t payload_mc_str[]        = "MC\r\n";
static uint8_t payload_start_str[]     = "START\r\n";
static uint8_t payload_powerdown_str[] = "POWERDOWN\r\n";

static payload_state_t payload_commanded = PAYLOAD_STATE_UNKNOWN;
static volatile bool payload_busy = false; // true while a command sequence is being sent

payload_state_t payload_get_commanded(void) { return payload_commanded; }
bool payload_is_busy(void) { return payload_busy; }

// Nortek break: wake characters, then the break string twice
static void payload_break(void)
{
  RTOS_ERR err;
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_wake_str, sizeof(payload_wake_str) - 1);
  OSTimeDly(PAYLOAD_WAKE_MS, OS_OPT_TIME_DLY, &err);
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_break_str, sizeof(payload_break_str) - 1);
  OSTimeDly(PAYLOAD_BREAK_GAP_MS, OS_OPT_TIME_DLY, &err);
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_break_str, sizeof(payload_break_str) - 1);
  OSTimeDly(PAYLOAD_REPLY_WAIT_MS, OS_OPT_TIME_DLY, &err); // TODO: read the reply banner instead
}


// TODO: payload_ctrl_meas() and payload_ctrl_sleep() block the calling task (~1.5 s). Consider in future a separate payload task so the controller keeps sampling.

// -----------------------------------------------------------------------------------

bool payload_init(void)
{
  CMU_ClockEnable(cmuClock_GPIO, true);
  GPIO_PinModeSet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN, gpioModePushPull, 1); // HIGH = instrument ON
  payload_commanded = PAYLOAD_STATE_MEASURING;

  // TEMP bench test: send the line once
  static uint8_t test_msg[] = "payload UART0 test\r\n";
  Ecode_t ec = UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, test_msg, sizeof(test_msg) - 1);
  printf("payload UART0 test sent, ecode=%lu\r\n", (unsigned long)ec);

  // TODO software switch: DONT assume state of payload.
  // Serial payload runs through MCU reset so on startup payload may be in measurement mode.
  // Use GETSTATE to query and set payload commanded based on the reply.
  // Set PAYLOAD_STATE_UNKNOWN if no reply.

  return true;
}

// sets the payload ON or in measurement mode
bool payload_ctrl_meas(void)
{
  RTOS_ERR err;
  payload_busy = true;

  GPIO_PinOutSet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN); // HIGH = instrument ON

  payload_break();
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_mc_str, sizeof(payload_mc_str) - 1);    // TODO: only if the break reply says Confirmation mode
  OSTimeDly(PAYLOAD_REPLY_WAIT_MS, OS_OPT_TIME_DLY, &err);                                           // TODO: wait for "OK" instead
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_start_str, sizeof(payload_start_str) - 1);
  payload_commanded = PAYLOAD_STATE_MEASURING;
  payload_busy = false;
  return true;
}

// sets payload OFF or out of measurement mode
bool payload_ctrl_sleep(void)
{
  RTOS_ERR err;
  payload_busy = true;

  GPIO_PinOutClear(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN); // LOW = instrument OFF
  payload_break();
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_mc_str, sizeof(payload_mc_str) - 1);    // TODO: only if the break reply says Confirmation mode
  OSTimeDly(PAYLOAD_REPLY_WAIT_MS, OS_OPT_TIME_DLY, &err);                                           // TODO: wait for "OK" instead
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_powerdown_str, sizeof(payload_powerdown_str) - 1);
  payload_commanded = PAYLOAD_STATE_SLEEP;
  payload_busy = false;
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

