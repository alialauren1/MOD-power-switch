/*
 * Keller_Pressure_Buffer.c
 *
 *  Created on: Apr 23, 2026
 *      Author: aliawolken
 */


#include "Keller_Pressure_Buffer.h"

static keller_sample_t buffer[KELLER_BUFFER_SIZE];
static int write_index  = 0;
static int read_index  = 0;
static int count = 0;

void keller_buffer_init(void) {
    write_index  = 0;
    read_index  = 0;
    count = 0; // how many samples in buffer
}

bool keller_buffer_store(int32_t p_mbar, int32_t t_centi, uint32_t t_ticks) { // writes to buffer[write_index] and appends write_index by 1
    if (count >= KELLER_BUFFER_SIZE) {
        return false;                                     // if count is over the keller buffer size, we drop samples
    }
    buffer[write_index].p_mbar  = p_mbar;
    buffer[write_index].t_centi = t_centi;
    buffer[write_index].t_ticks = t_ticks;
    write_index = (write_index + 1) % KELLER_BUFFER_SIZE; // % modulo makes buffer circular, when index reaches 16, it wraps back to zero
    count++;
    return true;
}

bool keller_buffer_retrieve(keller_sample_t *sample) { // copies buffer[read_index] into *sample, and appends read_index by 1
    if (count == 0) {
        return false;                                 // if count is zero, nothing to read so return false instead of reading garbage data from array
    }
    *sample = buffer[read_index];
    read_index = (read_index + 1) % KELLER_BUFFER_SIZE; // % modulo makes buffer circular, when index reaches 16, it wraps back to zero
    count--;
    return true;
}

bool keller_buffer_is_empty(void) {
    return count == 0;
}
