/*
 * task.h
 *
 *  Created on: Apr 21, 2026
 *      Author: aliawolken
 */

#ifndef TASK_H_
#define TASK_H_

// called by mod_executive_system.c in SYS_INIT_ACQ_TASKS
void get_sensor_data_task_create(void);
void retrieve_data_from_buffer_and_sd_store_task_create(void);
void retrieve_data_from_buffer2_and_single_read_task_create(void);
void button_stop_logging_task_create(void);

// called by mod_executive_system.c in SYS_ACQ stop sequence
void flush_sd_before_close(void);
void reset_block_avg_data_accumulators(void);

// called by mod_executive_system.c in SYS_CONFIG and cli.c with set_config
void config_sample_rate_task(unsigned int rate_hz);

// called by cli.c with set_config
unsigned int get_sample_rate_hz(void);

// called by mod_executive_system.c to park/resume acquisition tasks
void get_sensor_data_task_suspend_on_boot(void);
void get_sensor_data_task_resume(void);
void retrieve_task_suspend(void);
void retrieve_task_resume(void);
void button_stop_logging_task_suspend(void);
void button_stop_logging_task_resume(void);

void retrieve_buf2_task_suspend(void);
void retrieve_buf2_task_resume(void);

void sensor_request_state_reset(void);
void get_sensor_data_task_suspend(void);

bool keller_sensor_check(void);

#endif /* TASK_H_ */
