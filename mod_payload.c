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
#define PAYLOAD_BREAK_GAP_MS   600  // between the two "K1W%!Q" (from adcp.py)
//#define PAYLOAD_REPLY_WAIT_MS  500  // TEMP: stands in for reading the reply

#define PAYLOAD_OK_TIMEOUT_MS  5000  // give up waiting for "OK" after this long
#define PAYLOAD_POLL_MS          10  // how often to check for received bytes
#define PAYLOAD_RX_BUF_SIZE     256  // room for the longest reply

static uint8_t payload_wake_str[]      = "@@@@@@";
static uint8_t payload_break_str[]     = "K1W%!Q";
static uint8_t payload_mc_str[]        = "MC\r\n";
static uint8_t payload_start_str[]     = "START\r\n";
static uint8_t payload_powerdown_str[] = "POWERDOWN\r\n";

static uint8_t payload_inq_str[]       = "INQ\r\n";

static uint8_t payload_rx_buf[PAYLOAD_RX_BUF_SIZE];

static payload_state_t payload_commanded = PAYLOAD_STATE_UNKNOWN;
static volatile bool payload_busy = false; // true while a command sequence is being sent

payload_state_t payload_get_commanded(void) { return payload_commanded; }
bool payload_is_busy(void) { return payload_busy; }

// timing marker
bool payload_confirmed_measuring(void)
{
  if (GPIO_PinOutGet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN)) {
      return true;
  } else {
      return false;
  }
}

// start saving whatever the payload sends back on PC5 (call BEFORE sending)
static void payload_listen(void)
{
  UARTDRV_Receive(sl_uartdrv_usart_payload_handle, payload_rx_buf, sizeof(payload_rx_buf), NULL);
}

// wait for "OK" (true) or give up after PAYLOAD_OK_TIMEOUT_MS (false), then stop listening
static bool payload_wait_ok(void)
{
  RTOS_ERR err;
  uint8_t *rx_ptr;
  UARTDRV_Count_t received = 0;
  UARTDRV_Count_t remaining = 0;
  bool got_ok = false;

  for (uint32_t waited = 0; waited < PAYLOAD_OK_TIMEOUT_MS; waited += PAYLOAD_POLL_MS) {
      OSTimeDly(PAYLOAD_POLL_MS, OS_OPT_TIME_DLY, &err);
      UARTDRV_GetReceiveStatus(sl_uartdrv_usart_payload_handle, &rx_ptr, &received, &remaining);
      for (UARTDRV_Count_t i = 1; i < received; i++) {
          if (payload_rx_buf[i - 1] == 'O' && payload_rx_buf[i] == 'K') {
              got_ok = true;
          }
      }
      if (got_ok) {
          break;
      }
  }
  printf("payload reply (%lu bytes): %.*s\r\n", (unsigned long)received, (int)received, payload_rx_buf);
  UARTDRV_Abort(sl_uartdrv_usart_payload_handle, uartdrvAbortReceive); // stop listening
  return got_ok;
}

// Nortek break: wake characters, then the break string twice
static bool payload_break(void)
{
  RTOS_ERR err;
  bool ok = true;
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_wake_str, sizeof(payload_wake_str) - 1);
  OSTimeDly(PAYLOAD_WAKE_MS, OS_OPT_TIME_DLY, &err);
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_break_str, sizeof(payload_break_str) - 1);
  OSTimeDly(PAYLOAD_BREAK_GAP_MS, OS_OPT_TIME_DLY, &err);

  payload_listen();
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_break_str, sizeof(payload_break_str) - 1);
  if (!payload_wait_ok()) {
      printf("payload_break: no OK after break\r\n");
      ok = false;
  }
  return ok;
}

// ask the ADCP its mode without interrupting it (Integrator's Guide 5.31): true if it answers 0001 = measuring
static bool payload_inq_measuring(void)
{
  RTOS_ERR err;
  uint8_t *rx_ptr;
  UARTDRV_Count_t received = 0;
  UARTDRV_Count_t remaining = 0;
  bool measuring = false;

  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_wake_str, sizeof(payload_wake_str) - 1);
  OSTimeDly(400, OS_OPT_TIME_DLY, &err);   // guide: @@@@@@ <delay 400 ms> before INQ
  payload_listen();                        // fresh receive = the input flush the guide asks for
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_inq_str, sizeof(payload_inq_str) - 1);

  for (uint32_t waited = 0; waited < PAYLOAD_OK_TIMEOUT_MS; waited += PAYLOAD_POLL_MS) {
      OSTimeDly(PAYLOAD_POLL_MS, OS_OPT_TIME_DLY, &err);
      UARTDRV_GetReceiveStatus(sl_uartdrv_usart_payload_handle, &rx_ptr, &received, &remaining);
      for (UARTDRV_Count_t i = 3; i < received; i++) {
          if (payload_rx_buf[i - 3] == '0' && payload_rx_buf[i - 2] == '0' &&
              payload_rx_buf[i - 1] == '0' && payload_rx_buf[i] == '1') {
              measuring = true;
          }
      }
      if (measuring) {
          break;
      }
  }
  printf("payload INQ reply (%lu bytes): %.*s\r\n", (unsigned long)received, (int)received, payload_rx_buf);
  UARTDRV_Abort(sl_uartdrv_usart_payload_handle, uartdrvAbortReceive); // stop listening
  return measuring;
}

// TODO: payload_ctrl_meas() and payload_ctrl_sleep() block the calling task (~1.5 s). Consider in future a separate payload task so the controller keeps sampling.

// -----------------------------------------------------------------------------------

bool payload_init(void)
{
  CMU_ClockEnable(cmuClock_GPIO, true);
  GPIO_PinModeSet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN, gpioModePushPull, 0); // LOW until measuring is known

  // ADCP keeps its state through an MCU reset: leave it alone if already measuring, otherwise start it
  if (payload_inq_measuring()) {
      GPIO_PinOutSet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN); // timing marker: already measuring
      payload_commanded = PAYLOAD_STATE_MEASURING;
      printf("payload_init: ADCP already measuring\r\n");
  } else {
      printf("payload_init: ADCP not measuring, starting it\r\n");
      payload_ctrl_meas();
  }
  return true;
}

// sets the payload ON or in measurement mode
bool payload_ctrl_meas(void)
{
  bool ok = true;
  payload_busy = true;
  GPIO_PinOutSet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN); // timing marker: start of measure command sequence

  if (!payload_break()) {
      ok = false;
  }
  payload_listen();
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_mc_str, sizeof(payload_mc_str) - 1);    // TODO: only if the break reply says Confirmation mode
  if (!payload_wait_ok()) {
      printf("payload_ctrl_meas: no OK after MC\r\n");
      ok = false;
  }
  payload_listen();
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_start_str, sizeof(payload_start_str) - 1);
  if (!payload_wait_ok()) {
      printf("payload_ctrl_meas: no OK after START\r\n");
      ok = false;
  }

  if (ok) {
//      GPIO_PinOutSet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN); // HIGH = instrument ON
      payload_commanded = PAYLOAD_STATE_MEASURING;
  } else {
//      GPIO_PinOutClear(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN); // LOW = not confirmed measuring
      payload_commanded = PAYLOAD_STATE_UNKNOWN;
  }
  payload_busy = false;
  return ok;
}

// sets payload OFF or out of measurement mode
bool payload_ctrl_sleep(void)
{
  bool ok = true;
  payload_busy = true;
  GPIO_PinOutClear(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN); // timing marker: start of sleep command sequence

  if (!payload_break()) {
      ok = false;
  }
  payload_listen();
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_mc_str, sizeof(payload_mc_str) - 1);    // TODO: only if the break reply says Confirmation mode
  if (!payload_wait_ok()) {
      printf("payload_ctrl_sleep: no OK after MC\r\n");
      ok = false;
  }
  payload_listen();
  UARTDRV_TransmitB(sl_uartdrv_usart_payload_handle, payload_powerdown_str, sizeof(payload_powerdown_str) - 1);
  if (!payload_wait_ok()) {
      printf("payload_ctrl_sleep: no OK after POWERDOWN\r\n");
      ok = false;
  }

  if (ok) {
//      GPIO_PinOutClear(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN); // LOW = instrument OFF
      payload_commanded = PAYLOAD_STATE_SLEEP;
  } else {
//      GPIO_PinOutClear(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN); // LOW = not confirmed measuring
      payload_commanded = PAYLOAD_STATE_UNKNOWN;
  }
  payload_busy = false;
  return ok;
}

// checks status of payload
payload_state_t payload_status(void)
{
  // no GETSTATE yet: best info is the last command and whether all its OKs came back.
  // UNKNOWN after a failed command, so callers see "not measuring" and try again.
  return payload_commanded;
}

