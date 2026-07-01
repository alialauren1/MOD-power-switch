/*
 * Keller_Pressure_Buffer.h
 *
 *  Created on: Apr 23, 2026
 *      Author: aliawolken
 */

#ifndef SENSOR_DATA_BUFFER_H_
#define SENSOR_DATA_BUFFER_H_

#include <stdint.h>
#include <stdbool.h>

#define SENSOR_DATA_BUFFER_SIZE 256 // number of samples it can store

typedef struct {
    int32_t p_mbar;
    int32_t t_centi;
    uint64_t t_ticks;
    int hall;
} sensor_sample_t;

void sensor_data_buffer_init(void);
bool sensor_data_buffer_store(int32_t p_mbar, int32_t t_centi, uint64_t t_ticks, int hall);
bool sensor_data_buffer_retrieve(sensor_sample_t *sample);
bool sensor_data_buffer_is_empty(void);

void sensor_data_buffer2_store(int32_t p_mbar, int32_t t_centi, uint64_t t_ticks, int hall);
bool sensor_data_buffer2_retrieve(sensor_sample_t *sample);

#endif /* SENSOR_DATA_BUFFER_H_ */
