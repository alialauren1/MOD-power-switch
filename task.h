/*
 * task.h
 *
 *  Created on: Apr 21, 2026
 *      Author: aliawolken
 */

#ifndef TASK_H_
#define TASK_H_

void get_sensor_data_task_create(void);
void retrieve_data_from_buffer_and_sd_store_task_create(void);
void button_stop_logging_task_create(void);
void flush_sd_before_close(void);
void config_sample_rate_task(unsigned int rate_hz);
void reset_block_avg_data_accumulators(void);
unsigned int get_sample_rate_hz(void);

#endif /* TASK_H_ */
