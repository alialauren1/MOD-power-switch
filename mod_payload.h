/*
 * mod_payload.h
 *
 *  Created on: Sep 17, 2026
 *      Author: aliawolken
 *
 *      Payload functions to put the payload in different modes such as measuring/sampling or low power not sampling
 *
 *      Software (serial) control will be added onto these functions later.
 */

#ifndef MOD_PAYLOAD_H_
#define MOD_PAYLOAD_H_

#include <stdbool.h>

typedef enum {
  PAYLOAD_STATE_UNKNOWN = 0,  // zero-init means "we don't know", it should never be a claim
  PAYLOAD_STATE_MEASURING,    // in measurement mode, sampling and full power
  PAYLOAD_STATE_CMD,          // in command mode, not measuring but not in sleep, just "awake"
  PAYLOAD_STATE_SLEEP         // in low power mode
} payload_state_t;

bool payload_init(void);       // configure the mechanism, leave payload ON (fail-safe)
bool payload_ctrl_meas(void);  // command payload into measuring
bool payload_ctrl_sleep(void); // command payload into sleep

payload_state_t payload_status(void);        // query the payload
payload_state_t payload_get_commanded(void); // last commanded state, always a free read

bool payload_is_busy(void);                  // true while a command sequence is being sent

bool payload_confirmed_measuring(void);      // last confirmed state, matches the LED

#endif /* MOD_PAYLOAD_H_ */
