/*
 * Keller_Pressure_Buffer.c
 *
 *  Created on: Apr 23, 2026
 *      Author: aliawolken
 */


#include "Keller_Pressure_Buffer.h"

static keller_sample_t buffer[KELLER_BUFFER_SIZE];
static int head  = 0;
static int tail  = 0;
static int count = 0;

void keller_buffer_init(void) {
    head  = 0;
    tail  = 0;
    count = 0;
}

bool keller_buffer_store(int32_t p_mbar, int32_t t_centi, uint32_t t_ticks) {
    if (count >= KELLER_BUFFER_SIZE) {
        return false;
    }
    buffer[head].p_mbar  = p_mbar;
    buffer[head].t_centi = t_centi;
    buffer[head].t_ticks = t_ticks;
    head = (head + 1) % KELLER_BUFFER_SIZE;
    count++;
    return true;
}

bool keller_buffer_retrieve(keller_sample_t *sample) {
    if (count == 0) {
        return false;
    }
    *sample = buffer[tail];
    tail = (tail + 1) % KELLER_BUFFER_SIZE;
    count--;
    return true;
}

bool keller_buffer_is_empty(void) {
    return count == 0;
}
