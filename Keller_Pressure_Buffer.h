/*
 * Keller_Pressure_Buffer.h
 *
 *  Created on: Apr 23, 2026
 *      Author: aliawolken
 */

#ifndef KELLER_PRESSURE_BUFFER_H_
#define KELLER_PRESSURE_BUFFER_H_

#include <stdint.h>
#include <stdbool.h>

#define KELLER_BUFFER_SIZE 16 // number of samples it can store

typedef struct {
    int32_t p_mbar;
    int32_t t_centi;
    uint64_t t_ticks;
} keller_sample_t;

void keller_buffer_init(void);
bool keller_buffer_store(int32_t p_mbar, int32_t t_centi, uint64_t t_ticks);
bool keller_buffer_retrieve(keller_sample_t *sample);
bool keller_buffer_is_empty(void);

#endif /* KELLER_PRESSURE_BUFFER_H_ */
