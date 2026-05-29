/*
 * task.c
 *
 *  Created on: Apr 21, 2026
 *      Author: aliawolken
 *
 ********************************************************************************
 *
 *      The following are Micrium Specific:
 *        #include "os.h", #include "rtos_err.h",
 *        the ..._task_create() functions, OSTaskCreate(),
 *        DEF_NULL, OS_OPT_TASK_STK_CHK, OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR,
 *        OS_TCB, CPU_STK, RTOS_ERR
 *
 *        in keller_get_pressure_task(), the timing mechanism is Micrium OS specific because it allows non blocking of CPU:
 *          OSTimeDlyHMSM
 *          RTOS_ERR
 *
 *      The following is Silicon Labs specific:
 *        sl_sleeptimer_...()
 *
 *      TODO:
 *        If need to refactor for diff OS system, change ....
 *        timing
 *
 ********************************************************************************/


#include "os.h"
#include "rtos_err.h"
#include "task.h"
#include "sl_i2cspm.h"
#include "sl_i2cspm_instances.h"
#include "em_i2c.h"
#include "sl_sleeptimer.h"
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include "Keller_Pressure_Buffer.h"
#include <stdlib.h>
#include "mod_sd.h"
#include <string.h>

//For Keller_get_pressure_taskd
#define SENSOR_I2C_ADDR     0x40
//#define SAMPLE_INTERVAL_MS  9         // use min 8ms — satisfies 8ms minimum conversion time
#define STATUS_FIXED_BIT    (1 << 6)  // always 1 on real Keller sensor
#define STATUS_BUSY_BIT     (1 << 5)  // 1 = sensor still converting
#define STATUS_MEM_ERR_BIT  (1 << 2)  // 1 = internal checksum failed
#define P_OFFSET_MBAR 0 // calibration offset

#define SAMPLE_INTERVAL_MS  8
#define TOTAL_INTERVAL_MS   10 // per sample

#define SAMPLE_RATE_SEC 1 // minimum allowed is 0.01 seconds because that will be one sample
#define AVG_SAMPLE_COUNT ((SAMPLE_RATE_SEC*1000)/TOTAL_INTERVAL_MS) // amount of samples that we use to average before printing

typedef enum {
    STATE_WRITE,
    STATE_WAIT,
    STATE_READ,
    STATE_DELAY
} keller_state_t;

//For Keller_get_pressure_task_create
#define KELLER_GET_PRESSURE_TASK_PRIO      11u
#define KELLER_GET_PRESSURE_TASK_STK_SIZE  1024u
static CPU_STK keller_stk[KELLER_GET_PRESSURE_TASK_STK_SIZE];
static OS_TCB  keller_tcb;

//For Printing Pressure tasks
#define RETRIEVE_P_FROM_BUF_TASK_PRIO      20u
#define RETRIEVE_P_FROM_BUF_TASK_STK_SIZE  1024u
static CPU_STK retrieve_from_buf_stk[RETRIEVE_P_FROM_BUF_TASK_STK_SIZE];
static OS_TCB  retrieve_from_buf_tcb;

#define SD_BATCH_WRITE_SIZE 512
#define SD_SAMPLES_PER_WRITE 15
static char sd_batch_write_buf[SD_BATCH_WRITE_SIZE];
static int sd_batch_sample_count = 0;
static char data_array_for_sd_card[34]; // define at top of file and make static char array so doesn't use stack memory, possibly taking 80 bytes every run

//For Button task
#define BUTTON_TASK_PRIO      31u
#define BUTTON_TASK_STK_SIZE  512u
static CPU_STK button_stk[BUTTON_TASK_STK_SIZE];
static OS_TCB  button_tcb;

//For Error Message task
#define ERR_TASK_PRIO      35u
#define ERR_TASK_STK_SIZE  128u
static CPU_STK err_stk[ERR_TASK_STK_SIZE];
static OS_TCB  err_tcb;

//For Keller_get_pressure_task
static bool keller_p_sensor_init(void) // Safety formality: checks if sensor responds to its address being called
{ // Send a zero-length write to confirm the sensor is on the bus
  I2C_TransferSeq_TypeDef seq;
  uint8_t dummy = 0;  //0 is placeholder byte

  seq.addr        = SENSOR_I2C_ADDR << 1; // bit shift by 1 the sensor address to make room for R/W bit
  seq.flags       = I2C_FLAG_WRITE; // tells driver to set up write
  seq.buf[0].data = &dummy;         // Write buffer: pointer to data location (dummy since nothing sent)
  seq.buf[0].len  = 0;              //               zero bytes to send b/c only checking ACK, no data needed
  seq.buf[1].data = NULL;           // Read buffer: ignore because we're writing only here
  seq.buf[1].len  = 0;              //              also not used

  return (I2CSPM_Transfer(sl_i2cspm_sensor, &seq) == i2cTransferDone); // compare return value of I2C_TransferReturn_TypeDef to i2cTransferDone, if equal then true (1) gets returned, means successful.
}

//For Keller_get_pressure_task
static bool keller_p_sensor_trigger(void)
{
  I2C_TransferSeq_TypeDef seq;
  uint8_t cmd = 0xAC;           // Send 0xAC to start a conversion — result is ready after required millisecond duration

  seq.addr        = SENSOR_I2C_ADDR << 1; // who to talk to: sensor address & bit shifted by 1 the sensor address to make room for R/W bit
  seq.flags       = I2C_FLAG_WRITE;       // tells driver to set up write
  seq.buf[0].data = &cmd;                 // write buffer
  seq.buf[0].len  = 1;                    // amount of bytes
  seq.buf[1].data = NULL;
  seq.buf[1].len  = 0;

  return (I2CSPM_Transfer(sl_i2cspm_sensor, &seq) == i2cTransferDone); // compare return value of I2C_TransferReturn_TypeDef to i2cTransferDone, if equal then true (1) gets returned, means successful.
}

//For Keller_get_pressure_task
static bool keller_p_sensor_read(uint8_t *data, uint16_t len) // Read conversion result — 5 bytes: [status][P_hi][P_lo][T_hi][T_lo]
{
  I2C_TransferSeq_TypeDef seq;

  seq.addr        = SENSOR_I2C_ADDR << 1;   // who to talk to
  seq.flags       = I2C_FLAG_READ;          // tells driver to set up read
  seq.buf[0].data = data;                   // read data from buffer
  seq.buf[0].len  = len;                    // how many bytes
  seq.buf[1].data = NULL;
  seq.buf[1].len  = 0;

  return (I2CSPM_Transfer(sl_i2cspm_sensor, &seq) == i2cTransferDone); // compare return value of I2C_TransferReturn_TypeDef to i2cTransferDone, if equal then true (1) gets returned, means successful.
}

void keller_get_pressure_task(void *p_arg); // forward declaration
void retrieve_pressure_from_buffer_task(void *p_arg); // forward declaration
void button_task(void *p_arg); // forward declaration
void err_msg_task(void *p_arg); // forward declaration
//-------------------------------------------------------------------------------------------------------------

void keller_get_pressure_task_create(void) {
  RTOS_ERR err;

  OSTaskCreate(&keller_tcb,
               "Keller ACQ",
               keller_get_pressure_task,
               NULL,
               KELLER_GET_PRESSURE_TASK_PRIO,
               &keller_stk[0],
               (KELLER_GET_PRESSURE_TASK_STK_SIZE / 10u),
               KELLER_GET_PRESSURE_TASK_STK_SIZE,
               0u,
               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR,
               &err);
}

void keller_get_pressure_task(void *p_arg)
{
  (void)p_arg;

  RTOS_ERR delay_err;
  OSTimeDlyHMSM(0, 0, 5, 500, OS_OPT_TIME_HMSM_STRICT, &delay_err); // wait for SD card to boot up

  bool keller_p_sensor_ok = false;
  while(!keller_p_sensor_ok){
      keller_p_sensor_ok = keller_p_sensor_init();
      if(!keller_p_sensor_ok){
          printf("ERROR: No I2C ACK, retrying...\r\n");
          OSTimeDlyHMSM(0, 0, 0, 500, OS_OPT_TIME_HMSM_STRICT, &delay_err);
      }
  }

  printf("Sensor found at 0x%02X\r\n", SENSOR_I2C_ADDR);

  bool first_loop = true;
  bool data_processed = false;
  int32_t pressure_sum = 0;
  int32_t temp_sum = 0;
  int avg_sample_counter = 0;
  uint8_t raw[5];
  uint64_t t_ticks = 0;
  uint64_t t_ticks_mid = 0;
  uint32_t cycle_start = 0;
  uint32_t time_after_trigger = 0;
  uint32_t sample_interval_ticks = sl_sleeptimer_ms_to_tick(SAMPLE_INTERVAL_MS);
  uint32_t total_interval_ticks  = sl_sleeptimer_ms_to_tick(TOTAL_INTERVAL_MS);

  // initialize others inside the while loop. then inside just modify the value.
  uint8_t status = 0;

  uint16_t pressure = 0;
  uint16_t temp_raw = 0;
  int32_t p_mbar  = 0;
  int32_t t_centi = 0;

  uint32_t freq = 0;
  uint64_t t_sec_whole = 0;
  uint64_t t_sec_frac  = 0;

  keller_buffer_init();

  keller_state_t state = STATE_WRITE; // start on this state

  while (1){
      switch (state) {

          case STATE_WRITE: {
              cycle_start = sl_sleeptimer_get_tick_count();
              bool trigger_ok = keller_p_sensor_trigger();
              if (trigger_ok) {
                  time_after_trigger = sl_sleeptimer_get_tick_count();
                  state = STATE_WAIT;
              }
              else if (!trigger_ok) {
                  printf("ERROR: trigger write failed\r\n");
                  state = STATE_WRITE;
              }
              else {
                  printf("ERROR: unexpected write state condition\r\n");
              }
              break;
          }

          case STATE_WAIT: {
              uint32_t now = sl_sleeptimer_get_tick_count();
              if ((uint32_t)(now - time_after_trigger) >= sample_interval_ticks) {
                  state = STATE_READ;
              }
              else if ((uint32_t)(now - time_after_trigger) < sample_interval_ticks) {
                  if (!first_loop && !data_processed) {
                      status = raw[0];
                      if (!(status & STATUS_FIXED_BIT)) {
                          printf("ERROR: Bad status byte 0x%02X\r\n", status);
                      }
                      else if (status & STATUS_BUSY_BIT) {
                          printf("ERROR: Sensor busy\r\n");
                      }
                      else if (status & STATUS_MEM_ERR_BIT) {
                          printf("ERROR: Sensor memory error\r\n");
                      }
                      else {
                          pressure = (uint16_t)((raw[1] << 8) | raw[2]);
                          temp_raw = (uint16_t)((raw[3] << 8) | raw[4]);
                          p_mbar  = (int32_t)(((int64_t)pressure - 16384) * 100000 / 32768);
                          t_centi = ((int32_t)(temp_raw >> 4) - 24) * 5 - 5000;
                          pressure_sum += p_mbar;
                          temp_sum += t_centi;
                          avg_sample_counter++;
                          if (avg_sample_counter == (AVG_SAMPLE_COUNT+1)/2){ // integer division truncates so the +1 protects result if sample count is 1
                               t_ticks_mid = t_ticks; // store the time halfway through the averaging of samples
                          }
                          if (avg_sample_counter == AVG_SAMPLE_COUNT) {
                              if (keller_buffer_store(pressure_sum/AVG_SAMPLE_COUNT,
                                                  temp_sum/AVG_SAMPLE_COUNT,
                                                  t_ticks_mid)){
                              }
                              else {
                                  freq = sl_sleeptimer_get_timer_frequency(); // 32768 on EFM32GG11
                                  t_sec_whole = t_ticks / freq;
                                  t_sec_frac  = ((uint64_t)(t_ticks % freq) * 1000000) / freq;
                                  printf("WARNING: buffer full, sample dropped @ %02lu%06lu.%06lu\r\n",
                                         (uint32_t)(t_sec_whole / 1000000),
                                         (uint32_t)(t_sec_whole % 1000000),
                                         (uint32_t)t_sec_frac);
                                       }
                              pressure_sum = 0;
                              temp_sum = 0;
                              avg_sample_counter = 0;

                          }
                          data_processed = true;
                      }
                  }
                  RTOS_ERR yield_err;
                  uint32_t remaining = sample_interval_ticks - (uint32_t)(now - time_after_trigger);
                  uint32_t remaining_ms = sl_sleeptimer_tick_to_ms(remaining);
                  if (remaining_ms < 1) remaining_ms = 1;
                  OSTimeDly(remaining_ms, OS_OPT_TIME_DLY, &yield_err);
                  state = STATE_WAIT;
              }
              else {
                  printf("ERROR: unexpected wait state condition\r\n");
              }
              break;
          }

          case STATE_READ: {
              bool read_ok = keller_p_sensor_read(raw, sizeof(raw));
              t_ticks = sl_sleeptimer_get_tick_count64();
              if (read_ok) {
                  first_loop = false;
                  data_processed = false;
                  state = STATE_DELAY;
              }
              else if (!read_ok) {
                  printf("ERROR: I2C read failed\r\n");
                  state = STATE_READ;
              }
              else {
                  printf("ERROR: unexpected read state condition\r\n");
              }
              break;
          }

          case STATE_DELAY: {
              uint32_t now = sl_sleeptimer_get_tick_count();
              if ((uint32_t)(now - cycle_start) >= total_interval_ticks) {
                  state = STATE_WRITE;
              }
              else if ((uint32_t)(now - cycle_start) < total_interval_ticks) {
                  RTOS_ERR yield_err;
                  uint32_t remaining = total_interval_ticks - (uint32_t)(now - cycle_start);
                  uint32_t remaining_ms = sl_sleeptimer_tick_to_ms(remaining);
                  if (remaining_ms < 1) remaining_ms = 1;
                  OSTimeDly(remaining_ms, OS_OPT_TIME_DLY, &yield_err);
                  state = STATE_DELAY;
              }
              else {
                  printf("ERROR: unexpected delay state condition\r\n");
              }
              break;
          }
      }
  }
}

void retrieve_pressure_from_buffer_task_create(void) {
  RTOS_ERR err;

  OSTaskCreate(&retrieve_from_buf_tcb,
               "RetrieveFromBuf",
               retrieve_pressure_from_buffer_task,
               NULL,
               RETRIEVE_P_FROM_BUF_TASK_PRIO,
               &retrieve_from_buf_stk[0],
               (RETRIEVE_P_FROM_BUF_TASK_STK_SIZE / 10u),
               RETRIEVE_P_FROM_BUF_TASK_STK_SIZE,
               0u,

               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR,
               &err);
}


void retrieve_pressure_from_buffer_task(void *p_arg) {
  (void)p_arg;
  RTOS_ERR err;

  while (1) {

      // drain circular buffer and printf
      keller_sample_t sample;

      while (keller_buffer_retrieve(&sample)) {

          if (!mod_sd_is_open_AW()){
             break; // if SD card not open, exit loop
          }

          uint32_t freq = sl_sleeptimer_get_timer_frequency(); // 32768 on EFM32GG11
          uint64_t t_sec_whole = sample.t_ticks / freq;
          uint64_t t_sec_frac  = ((sample.t_ticks % freq) * 1000000) / freq;

          int len = snprintf(data_array_for_sd_card, sizeof(data_array_for_sd_card),
                             "%c%03d.%03d,%03d.%02d,%02lu%06lu.%06lu\r\n",
                             (sample.p_mbar<0 ? '-':' '),
                             (int)(abs(sample.p_mbar) / 1000),
                             (int)(abs(sample.p_mbar) % 1000),
                             (int)((sample.t_centi * 9 / 5 + 3200) / 100),
                             (int)((sample.t_centi * 9 / 5 + 3200) % 100),
                             (uint32_t)(t_sec_whole / 1000000),
                             (uint32_t)(t_sec_whole % 1000000),
                             (uint32_t)t_sec_frac);

          memcpy(sd_batch_write_buf+(sd_batch_sample_count*len), data_array_for_sd_card,len); // copy "len" from "data_array_for_sd_card" into "sd_batch_write_buf+(sd_batch_sample_count*len)"
                                                                                              // ^ the addition moves destination pointer forward by bytes already accumulated
          sd_batch_sample_count++;

          if (sd_batch_sample_count >= SD_SAMPLES_PER_WRITE){
              uint32_t write_start = sl_sleeptimer_get_tick_count();
              bool write_ok = mod_sd_write_AW(sd_batch_write_buf,sd_batch_sample_count*len);
              uint32_t write_end = sl_sleeptimer_get_tick_count();
              uint32_t write_ms = sl_sleeptimer_tick_to_ms(write_end - write_start);
//              printf("SD W: %lu ms\r\n", write_ms);
              if (!write_ok){
                  printf("Write failed for buffer \r\n");
              }
              sd_batch_sample_count=0;
          }
      }
// Put in when put in button:
//           uint32_t write_start = sl_sleeptimer_get_tick_count();
//          bool write_ok = mod_sd_write_AW(data_array_for_sd_card, len);
//          uint32_t write_end = sl_sleeptimer_get_tick_count();
//          uint32_t write_ms = sl_sleeptimer_tick_to_ms(write_end - write_start);
//          printf("SD W: %lu ms\r\n", write_ms);
//
//          if (!write_ok){
//              printf("Write failed for sample \r\n");
//          }
//
//          }

      OSTimeDly(TOTAL_INTERVAL_MS/2, OS_OPT_TIME_DLY, &err);
  }
}



void button_task_create(void) {
  RTOS_ERR err;

  OSTaskCreate(&button_tcb,
               "Button",
               button_task,
               NULL,
               BUTTON_TASK_PRIO,
               &button_stk[0],
               (BUTTON_TASK_STK_SIZE / 10u),
               BUTTON_TASK_STK_SIZE,
               0u,
               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR,
               &err);
}

void button_task(void *p_arg) {
  (void)p_arg;
  RTOS_ERR err;
//  static uint32_t btn_low_count = 0;
  while (1) {
      if (GPIO_PinInGet(gpioPortC, 8) == 0 && mod_sd_is_open_AW()) {
          mod_sd_close_and_unmount_AW();
      }

//      if (GPIO_PinInGet(gpioPortC, 8) == 0) {
//          btn_low_count++;
//          if (btn_low_count >= 5 && mod_sd_is_open_AW()) {
//              mod_sd_close_and_unmount_AW();
//              btn_low_count = 0;
//          }
//      } else {
//          btn_low_count = 0;
//      }
      OSTimeDly(50, OS_OPT_TIME_DLY, &err); // poll every 50ms

      // OSTaskDelete put here
  }
}

void err_msg_task_create(void) {
  RTOS_ERR err;

  OSTaskCreate(&err_tcb,
               "Error",
               err_msg_task,
               NULL,
               ERR_TASK_PRIO,
               &err_stk[0],
               (ERR_TASK_STK_SIZE / 10u),
               ERR_TASK_STK_SIZE,
               0u,
               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR,
               &err);
}

void err_msg_task(void *p_arg){
  (void)p_arg;
  RTOS_ERR err;
  while (1) {
      OSTimeDly(500, OS_OPT_TIME_DLY, &err); // poll every 500ms
  }
}
