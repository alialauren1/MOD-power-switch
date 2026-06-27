/*
 * mod_executive_system.h
 *
 *  Created on: Jun 15, 2026
 *      Author: aliawolken
 */

#ifndef MOD_EXECUTIVE_SYSTEM_H_
#define MOD_EXECUTIVE_SYSTEM_H_

#include <stdbool.h>

#define SAMPLE_RATE_HZ_DEFAULT 100 // default, allowable range is 1 Hz (1 s) to 100 Hz (0.01 sec)

typedef enum {
  SYS_STARTUP,
  SYS_INIT_INFRA_TASKS,
  SYS_CONFIG,
  SYS_MEM,
  SYS_INIT_ACQ_TASKS,
  SYS_SELF_CHECK,
  SYS_RUNNING_MODE_CHECK_AND_IDLE,
  SYS_ACQU,
  SYS_ERR
} system_state_t;

typedef enum{
  RUNNING_MODE_IDLE,
  RUNNING_MODE_AUTO_CONTROL_AND_LOG
} running_mode_t;

typedef struct {
  unsigned int sample_rate_hz;
  bool         logging_on_flg;
  bool         controller_on_flg;
} run_time_variables_t;

void           system_executive_task_create(void);
void           system_request_start_acquisition(void);
void           system_request_stop_acquisition(void);
void           system_request_single_read(void);
void           system_clear_single_read_flag(void);
system_state_t system_get_state(void);
running_mode_t system_get_running_mode(void);
bool system_get_single_read_flag(void);

#endif /* MOD_EXECUTIVE_SYSTEM_H_ */
