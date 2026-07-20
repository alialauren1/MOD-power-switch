/*
 * Sensor_Data_Buffer.c
 *
 *  Created on: Apr 23, 2026
 *      Author: aliawolken
 */


#include <Sensor_Data_Buffer.h>
#include "em_core.h"   // <-- ADD THIS for CORE_ENTER_ATOMIC / CORE_EXIT_ATOMIC

static sensor_sample_t buffer[SENSOR_DATA_BUFFER_SIZE];
static int write_index  = 0;
static int read_index  = 0;
static volatile int count = 0;       // <-- volatile because both tasks read/write it

static sensor_sample_t sensor_data_buffer2;
static bool sensor_data_buffer2_ready = false;

static sensor_sample_t sensor_data_buffer3;
static bool sensor_data_buffer3_ready = false;
// -----------------------------------------

void sensor_data_buffer_init(void) {
    write_index  = 0;
    read_index  = 0;
    count = 0; // how many samples in buffer
}

bool sensor_data_buffer_store(int32_t p_mbar, int32_t t_centi, uint64_t t_ticks, int hall,int ctrl_out) { // writes to buffer[write_index] and appends write_index by 1
    CORE_DECLARE_IRQ_STATE; // declares saved interrupt state variable

    // Atomic check if buffer is full
    CORE_ENTER_ATOMIC();
    if (count >= SENSOR_DATA_BUFFER_SIZE) {
        CORE_EXIT_ATOMIC();
        return false;                                     // if count is over the sensor buffer size, we drop samples
    }
    CORE_EXIT_ATOMIC();

    // write data to slot, no racing only writing to write_index
    buffer[write_index].p_mbar  = p_mbar;
    buffer[write_index].t_centi = t_centi;
    buffer[write_index].t_ticks = t_ticks;
    buffer[write_index].hall = hall;
    buffer[write_index].ctrl_out = ctrl_out;
    write_index = (write_index + 1) % SENSOR_DATA_BUFFER_SIZE; // % modulo makes buffer circular, when index reaches 16, it wraps back to zero

    // Atomic increment of count
    CORE_ENTER_ATOMIC();
    count++;
    CORE_EXIT_ATOMIC();
    return true;
}

bool sensor_data_buffer_retrieve(sensor_sample_t *sample) { // copies buffer[read_index] into *sample, and appends read_index by 1
    CORE_DECLARE_IRQ_STATE; // declares saved interrupt state variable

    // Atomic check if buffer is empty
    CORE_ENTER_ATOMIC();
    if (count == 0) {
        CORE_EXIT_ATOMIC();
        return false;                                 // if count is zero, nothing to read so return false instead of reading garbage data from array
    }
    CORE_EXIT_ATOMIC();

    // read data from slot, no racing on writes read_index
    *sample = buffer[read_index];
    read_index = (read_index + 1) % SENSOR_DATA_BUFFER_SIZE; // % modulo makes buffer circular, when index reaches 16, it wraps back to zero

    // Atomic increment of count
    CORE_ENTER_ATOMIC();
    count--;
    CORE_EXIT_ATOMIC();
    return true;
}

// -----------------------------------------

void sensor_data_buffer2_store(int32_t p_mbar, int32_t t_centi, uint64_t t_ticks, int hall,int ctrl_out) {
    sensor_data_buffer2.p_mbar  = p_mbar;
    sensor_data_buffer2.t_centi = t_centi;
    sensor_data_buffer2.t_ticks = t_ticks;
    sensor_data_buffer2.hall    = hall;
    sensor_data_buffer2.ctrl_out = ctrl_out;
    sensor_data_buffer2_ready   = true; // marks that sensor has stored a real reading
}

bool sensor_data_buffer2_retrieve(sensor_sample_t *sample) {
    if (!sensor_data_buffer2_ready) return false; // wait until sensor has stored a real reading
    *sample = sensor_data_buffer2;
    sensor_data_buffer2_ready = false; // consumed — next retrieve will wait for the next store
    return true;
    // single_read_sensor_flag is cleared separately by the task after printing
}


// -----------------------------------------

void sensor_data_buffer3_store(int32_t p_mbar, int32_t t_centi, uint64_t t_ticks, int hall,int ctrl_out) {
    sensor_data_buffer3.p_mbar  = p_mbar;
    sensor_data_buffer3.t_centi = t_centi;
    sensor_data_buffer3.t_ticks = t_ticks;
    sensor_data_buffer3.hall    = hall;
    sensor_data_buffer3.ctrl_out = ctrl_out;
    sensor_data_buffer3_ready   = true; // marks that sensor has stored a real reading
}

bool sensor_data_buffer3_retrieve(sensor_sample_t *sample) {
    if (!sensor_data_buffer3_ready) return false; // wait until sensor has stored a real reading
    *sample = sensor_data_buffer3;
    sensor_data_buffer3_ready = false; // consumed — next retrieve will wait for the next store
    return true;
    // single_read_sensor_flag is cleared separately by the task after printing
}
