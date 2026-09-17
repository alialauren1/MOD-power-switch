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

#define PAYLOAD_OUTPUT_PORT  gpioPortA
#define PAYLOAD_OUTPUT_PIN   13

static payload_state_t payload_commanded = PAYLOAD_STATE_UNKNOWN;

payload_state_t payload_get_commanded(void) { return payload_commanded; }

bool payload_init(void)
{
  CMU_ClockEnable(cmuClock_GPIO, true);
  GPIO_PinModeSet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN, gpioModePushPull, 1); // HIGH = instrument ON
  payload_commanded = PAYLOAD_STATE_MEASURING;

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
  return GPIO_PinOutGet(PAYLOAD_OUTPUT_PORT, PAYLOAD_OUTPUT_PIN)
           ? PAYLOAD_STATE_MEASURING
           : PAYLOAD_STATE_SLEEP;
}

