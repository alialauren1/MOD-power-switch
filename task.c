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
#include <stdlib.h>
#include "mod_sd.h"
#include <string.h>
#include <Sensor_Data_Buffer.h>
#include "em_gpio.h"
#include "em_cmu.h"
#include "mod_executive_system.h"

//For Keller_get_pressure_taskd
#define SENSOR_I2C_ADDR     0x40

#define STATUS_FIXED_BIT    (1 << 6)  // always 1 on real Keller sensor
#define STATUS_BUSY_BIT     (1 << 5)  // 1 = sensor still converting
#define STATUS_MEM_ERR_BIT  (1 << 2)  // 1 = internal checksum failed
#define P_OFFSET_MBAR 0 // calibration offset

#define SAMPLE_INTERVAL_MS  8
#define TOTAL_INTERVAL_MS   10 // per sample

#define AVG_SAMPLE_COUNT_DEFAULT ((1000 / SAMPLE_RATE_HZ_DEFAULT) / TOTAL_INTERVAL_MS); // amnt of samples used to avg, calculated from sample_rate_hz after config loads

static uint32_t sample_rate_hz  = SAMPLE_RATE_HZ_DEFAULT;    // default, overwritten by config file on startup
static uint32_t avg_sample_count = AVG_SAMPLE_COUNT_DEFAULT;  // default, recalculated by apply_config_sample_rate_task() after config loads
static int32_t  pressure_sum     = 0;
static int32_t  temp_sum         = 0;
static uint32_t avg_sample_counter = 0;
static uint32_t depth_bottom_turnaround_counter = 0;

#define HALL_EFFECT_PORT  gpioPortA   // port hall effect signal is attached to
#define HALL_EFFECT_PIN   12           // pin hall effect signal is attached to
#define HALL_EFFECT_IDLE_STATE 1 // 1 = naturally HIGH (active-low output), 0 = naturally LOW (active high output)

#define HALL_EFFECT_DESCENT_STATE 0 // when magnet is aligned, output it low, system on descent
#define HALL_EFFECT_ASCENT_STATE (!HALL_EFFECT_DESCENT_STATE)

#define CONTROLLER_OUTPUT_PORT  gpioPortA
#define CONTROLLER_OUTPUT_PIN   13 // active-low "cut power" line: HIGH (default) = instrument ON (fail-safe), LOW = instrument OFF

typedef enum {
    STATE_WRITE,
    STATE_WAIT,
    STATE_READ,
    STATE_DELAY
} sensor_state_t;

//For Keller_get_pressure_task_create
#define GET_SENSOR_DATA_TASK_PRIO      11u
#define GET_SENSOR_DATA_TASK_STK_SIZE  1024u
static CPU_STK sensor_stk[GET_SENSOR_DATA_TASK_STK_SIZE];
static OS_TCB  sensor_tcb;

//For Printing Pressure tasks
#define RETRIEVE_DATA_FROM_BUF_TASK_PRIO      20u
#define RETRIEVE_DATA_FROM_BUF_TASK_STK_SIZE  1024u
static CPU_STK retrieve_from_buf_stk[RETRIEVE_DATA_FROM_BUF_TASK_STK_SIZE];
static OS_TCB  retrieve_from_buf_tcb;

//For Printing Pressure tasks
#define RETRIEVE_DATA_FROM_BUF2_TASK_PRIO      25u
#define RETRIEVE_DATA_FROM_BUF2_TASK_STK_SIZE  1024u
static CPU_STK retrieve_from_buf2_stk[RETRIEVE_DATA_FROM_BUF2_TASK_STK_SIZE];
static OS_TCB  retrieve_from_buf2_tcb;

//For controller_task
#define CONTROLLER_TASK_PRIO      15u
#define CONTROLLER_TASK_STK_SIZE  1024u
static CPU_STK controller_stk[CONTROLLER_TASK_STK_SIZE];
static OS_TCB  controller_tcb;

typedef enum {
    STATE_PROFILE_EST,
    STATE_ON_AND_WAIT,
    STATE_TURN_OFF,
    STATE_OFF_AND_WAIT,
    STATE_TURN_ON
} controller_state_t;

#define SD_BUF_WRITE_SIZE 512
#define SD_SAMPLES_PER_WRITE 13
static char sd_write_buf[SD_BUF_WRITE_SIZE];
static int sd_buffer_sample_count = 0;
static int sd_bytes_merged = 0;
static char data_array_for_sd_card[38]; // define at top of file and make static char array so doesn't use stack memory, possibly taking 80 bytes every run

//For Button task
#define BUTTON_STOP_ACQU_TASK_PRIO      31u
#define BUTTON_STOP_ACQU_TASK_STK_SIZE  512u
static CPU_STK button_stop_acqu_stk[BUTTON_STOP_ACQU_TASK_STK_SIZE];
static OS_TCB  button_stop_acqu_tcb;

//For get_sensor_data_task
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

void get_sensor_data_task(void *p_arg); // forward declaration
void retrieve_data_from_buffer_and_sd_store_task(void *p_arg); // forward declaration
void retrieve_data_from_buffer2_and_single_read_task(void *p_arg); // forward declaration
void button_stop_acqu_task(void *p_arg); // forward declaration
void controller_task(void *p_arg); // forward declaration

//----------------------------------Sub Tasks--------------------------------------------------------------

void clear_acqu_data_accumulators(void){
  pressure_sum       = 0;
  temp_sum           = 0;
  avg_sample_counter = 0;
  sensor_sample_t discard;
  while (sensor_data_buffer_retrieve(&discard)) {}
}

void flush_sd_before_close(void){
  if (sd_buffer_sample_count>0 && mod_sd_is_open_AW()) {       // sample count should be reset to zero if fully written to sd card, mod_sd_is_open_AW should return 1 if open
      mod_sd_write_AW(sd_write_buf,sd_bytes_merged);    // write to sd card what didn't get written as a full batch write
      sd_buffer_sample_count=0;
      sd_bytes_merged = 0;
  }
  else{
      printf("No flush of data to sd card needed\r\n");
  }
}

unsigned int get_sample_rate_hz(void){
  return sample_rate_hz;
}

void config_sample_rate_task(unsigned int rate_hz) {
    if (rate_hz < 1 || rate_hz > 100) {
        rate_hz = SAMPLE_RATE_HZ_DEFAULT;
        printf("Sample rate out of range, Substituting default\r\n");
    }
    avg_sample_count   = (1000 / rate_hz) / TOTAL_INTERVAL_MS;
    if (avg_sample_count < 1) avg_sample_count =1;

    sample_rate_hz = 1000/(avg_sample_count*TOTAL_INTERVAL_MS);
    if (sample_rate_hz!= rate_hz){
        printf("Sample rate %u Hz not achievable, rounded up to %lu Hz\r\n", rate_hz, sample_rate_hz);
    }
}


void get_sensor_data_task_suspend_on_boot(void) { RTOS_ERR err; OSTaskSuspend(&sensor_tcb, &err); EFM_ASSERT(err.Code == RTOS_ERR_NONE);}
void get_sensor_data_task_resume(void)  { RTOS_ERR err; OSTaskResume(&sensor_tcb, &err); }
void retrieve_task_suspend(void)        { RTOS_ERR err; OSTaskSuspend(&retrieve_from_buf_tcb, &err); EFM_ASSERT(err.Code == RTOS_ERR_NONE);}
void retrieve_task_resume(void)         { RTOS_ERR err; OSTaskResume(&retrieve_from_buf_tcb, &err); }
void retrieve_buf2_task_suspend(void)        { RTOS_ERR err; OSTaskSuspend(&retrieve_from_buf2_tcb, &err); EFM_ASSERT(err.Code == RTOS_ERR_NONE);}
void retrieve_buf2_task_resume(void)         { RTOS_ERR err; OSTaskResume(&retrieve_from_buf2_tcb, &err); }
void button_stop_acqu_task_suspend(void) { RTOS_ERR err; OSTaskSuspend(&button_stop_acqu_tcb, &err); EFM_ASSERT(err.Code == RTOS_ERR_NONE);}
void button_stop_acqu_task_resume(void)  { RTOS_ERR err; OSTaskResume(&button_stop_acqu_tcb, &err); }
void controller_task_suspend(void) { RTOS_ERR err; OSTaskSuspend(&controller_tcb, &err); EFM_ASSERT(err.Code == RTOS_ERR_NONE);}
void controller_task_resume(void)  { RTOS_ERR err; OSTaskResume(&controller_tcb, &err); }

static bool sensor_state_reset_on_resume = false;
void sensor_request_state_reset(void) { sensor_state_reset_on_resume = true; }

bool keller_sensor_check(void) { return keller_p_sensor_init(); }

static bool controller_print_config_on_resume = false; // flag for telling when we reenter controller task so we can print config
void controller_request_print_config(void) { controller_print_config_on_resume = true; }

static sensor_state_t sensor_task_state = STATE_WRITE; // start on this state
static controller_state_t controller_task_state = STATE_PROFILE_EST;

void get_sensor_data_task_suspend(void) {
    RTOS_ERR err;
    while (sensor_task_state != STATE_DELAY) {
        OSTimeDly(1, OS_OPT_TIME_DLY, &err);
    }
    OSTaskSuspend(&sensor_tcb, &err);
}

//-----------------------------Acquisition Tasks-----------------------------------------------------

void get_sensor_data_task_create(void) {
  RTOS_ERR err;

  OSTaskCreate(&sensor_tcb,
               "Sensor ACQ",
               get_sensor_data_task,
               NULL,
               GET_SENSOR_DATA_TASK_PRIO,
               &sensor_stk[0],
               (GET_SENSOR_DATA_TASK_STK_SIZE / 10u),
               GET_SENSOR_DATA_TASK_STK_SIZE,
               0u,
               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR,
               &err);
}

void get_sensor_data_task(void *p_arg)
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
  uint8_t raw[5];
  uint64_t t_ticks = 0;
  uint64_t t_ticks_mid = 0;
  uint32_t cycle_start = 0;
  uint32_t time_after_trigger = 0;
  uint32_t sample_interval_ticks = sl_sleeptimer_ms_to_tick(SAMPLE_INTERVAL_MS);
  uint32_t total_interval_ticks  = sl_sleeptimer_ms_to_tick(TOTAL_INTERVAL_MS);
  int ctrl_out_midway = 0;

  // initialize others inside the while loop. then inside just modify the value.
  uint8_t status = 0;

  uint16_t pressure = 0;
  uint16_t temp_raw = 0;
  int32_t p_mbar  = 0;
  int32_t t_centi = 0;

  uint32_t freq = 0;
  uint64_t t_sec_whole = 0;
  uint64_t t_sec_frac  = 0;

  int hall_midway = 0; // TODO: well 0 is an answer so figure out what happens if its the zero from init vs 0 from sensor reading
  int hall_raw = 0;

  sensor_data_buffer_init();

  CMU_ClockEnable(cmuClock_GPIO, true);
  GPIO_PinModeSet(HALL_EFFECT_PORT, HALL_EFFECT_PIN, gpioModeInputPull, HALL_EFFECT_IDLE_STATE); // high at first, when pulled low will detect 0

  while (1){

      if (sensor_state_reset_on_resume){
          sensor_task_state = STATE_WRITE;
          first_loop = true;
          sensor_state_reset_on_resume = false;
      }

      switch (sensor_task_state) {

          case STATE_WRITE: {
              cycle_start = sl_sleeptimer_get_tick_count();
              bool trigger_ok = keller_p_sensor_trigger();
              if (trigger_ok) {
                  time_after_trigger = sl_sleeptimer_get_tick_count();
                  sensor_task_state = STATE_WAIT;
              }
              else if (!trigger_ok) {
                  printf("ERROR: trigger write failed\r\n");
                  sensor_task_state = STATE_DELAY;
              }
              else {
                  printf("ERROR: unexpected write state condition\r\n");
              }
              break;
          }

          case STATE_WAIT: {
              uint32_t now = sl_sleeptimer_get_tick_count();
              if ((uint32_t)(now - time_after_trigger) >= sample_interval_ticks) {
                  sensor_task_state = STATE_READ;
              }
              else if ((uint32_t)(now - time_after_trigger) < sample_interval_ticks) {
                  if (!first_loop && !data_processed) {
                      data_processed = true;
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
                          if (avg_sample_counter == (avg_sample_count+1)/2){            // integer division truncates so the +1 protects result if sample count is 1
                               t_ticks_mid = t_ticks;                                   // store the time halfway through the averaging of samples
                               hall_midway = hall_raw;                                      // store the direction when we will be recording the midway sample
                               ctrl_out_midway = GPIO_PinOutGet(CONTROLLER_OUTPUT_PORT, CONTROLLER_OUTPUT_PIN);
                          }
                          if (avg_sample_counter == avg_sample_count) {
                              if (sensor_data_buffer_store(pressure_sum/(int32_t)avg_sample_count,
                                                  temp_sum/(int32_t)avg_sample_count,
                                                  t_ticks_mid,
                                                  hall_midway,
                                                  ctrl_out_midway)){ }
                              else {
                                  freq = sl_sleeptimer_get_timer_frequency();           // 32768 on EFM32GG11
                                  t_sec_whole = t_ticks / freq;
                                  t_sec_frac  = ((uint64_t)(t_ticks % freq) * 1000000) / freq;
                                  printf("WARNING: buffer full, sample dropped @ %02lu%06lu.%06lu\r\n",
                                         (uint32_t)(t_sec_whole / 1000000),
                                         (uint32_t)(t_sec_whole % 1000000),
                                         (uint32_t)t_sec_frac);
                                       }

                              // single read buffer
                              if (system_get_single_read_flag()){
                                  (sensor_data_buffer2_store(pressure_sum/(int32_t)avg_sample_count,
                                                             temp_sum/(int32_t)avg_sample_count,
                                                             t_ticks_mid,
                                                             hall_midway,
                                                             ctrl_out_midway));
                              }

                              // controller buffer
                              sensor_data_buffer3_store(pressure_sum/(int32_t)avg_sample_count,
                                                         temp_sum/(int32_t)avg_sample_count,
                                                         t_ticks_mid,
                                                         hall_midway,
                                                         ctrl_out_midway);

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
                  sensor_task_state = STATE_WAIT;
              }
              else {
                  printf("ERROR: unexpected wait state condition\r\n");
              }
              break;
          }

          case STATE_READ: {
              bool read_ok = keller_p_sensor_read(raw, sizeof(raw));
              t_ticks = sl_sleeptimer_get_tick_count64();
              hall_raw = GPIO_PinInGet(HALL_EFFECT_PORT, HALL_EFFECT_PIN); // Read Value of hall effect sensor
              if (read_ok) {
                  first_loop = false;
                  data_processed = false;
                  sensor_task_state = STATE_DELAY;
              }
              else if (!read_ok) {
                  printf("ERROR: I2C read failed\r\n");
                  sensor_task_state = STATE_DELAY;
              }
              else {
                  printf("ERROR: unexpected read state condition\r\n");
              }
              break;
          }

          case STATE_DELAY: {
              uint32_t now = sl_sleeptimer_get_tick_count();
              if ((uint32_t)(now - cycle_start) >= total_interval_ticks) {
                  sensor_task_state = STATE_WRITE;
              }
              else if ((uint32_t)(now - cycle_start) < total_interval_ticks) {
                  RTOS_ERR yield_err;
                  uint32_t remaining = total_interval_ticks - (uint32_t)(now - cycle_start);
                  uint32_t remaining_ms = sl_sleeptimer_tick_to_ms(remaining);
                  if (remaining_ms < 1) remaining_ms = 1;
                  OSTimeDly(remaining_ms, OS_OPT_TIME_DLY, &yield_err);
                  sensor_task_state = STATE_DELAY;
              }
              else {
                  printf("ERROR: unexpected delay state condition\r\n");
              }
              break;
          }
      }
  }
}

void retrieve_data_from_buffer_and_sd_store_task_create(void) {
  RTOS_ERR err;

  OSTaskCreate(&retrieve_from_buf_tcb,
               "RetrieveFromBuf",
               retrieve_data_from_buffer_and_sd_store_task,
               NULL,
               RETRIEVE_DATA_FROM_BUF_TASK_PRIO,
               &retrieve_from_buf_stk[0],
               (RETRIEVE_DATA_FROM_BUF_TASK_STK_SIZE / 10u),
               RETRIEVE_DATA_FROM_BUF_TASK_STK_SIZE,
               0u,

               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR,
               &err);

}


void retrieve_data_from_buffer_and_sd_store_task(void *p_arg) {
  (void)p_arg;
  RTOS_ERR err;

  while (1) {

      // drain circular buffer and printf
      sensor_sample_t sample; // keller_buffer_Store holds the block averaged samples

      while (sensor_data_buffer_retrieve(&sample)) {

          uint32_t freq = sl_sleeptimer_get_timer_frequency();                                // 32768 on EFM32GG11
          uint64_t t_sec_whole = sample.t_ticks / freq;
          uint64_t t_sec_frac  = ((sample.t_ticks % freq) * 1000000) / freq;

          if (system_get_running_mode()==RUNNING_MODE_AUTO_CONTROL_AND_LOG){
              if (!mod_sd_is_open_AW()){
                  mod_sd_write_AW(NULL,0); // attempt recovery
                  break; // if SD card not open, exit loop
              }

              int len = snprintf(data_array_for_sd_card, sizeof(data_array_for_sd_card),
                                           "%c%03d.%03d,%03d.%02d,%02lu%06lu.%06lu,%d,%d\r\n",
                                           (sample.p_mbar<0 ? '-':' '),
                                           (int)(abs(sample.p_mbar) / 1000),
                                           (int)(abs(sample.p_mbar) % 1000),
                                           (int)((sample.t_centi * 9 / 5 + 3200) / 100),
                                           (int)((sample.t_centi * 9 / 5 + 3200) % 100),
                                           (uint32_t)(t_sec_whole / 1000000),
                                           (uint32_t)(t_sec_whole % 1000000),
                                           (uint32_t)t_sec_frac,
                                           sample.hall,
                                           sample.ctrl_out);

              memcpy(sd_write_buf+(sd_buffer_sample_count*len), data_array_for_sd_card,len); // copy "len" from "data_array_for_sd_card" into "sd_batch_write_buf+(sd_batch_sample_count*len)"
                                                                                                  // ^ the addition moves destination pointer forward by bytes already accumulated
              sd_buffer_sample_count++;
              sd_bytes_merged += len; // bytes written to data_array_for_sd_card

              if (sd_buffer_sample_count >= SD_SAMPLES_PER_WRITE){
                  bool write_ok = mod_sd_write_AW(sd_write_buf,sd_buffer_sample_count*len);
                  if (!write_ok){
                      printf("Write failed for buffer \r\n");
                  }
                  sd_buffer_sample_count=0;
                  sd_bytes_merged = 0;
              }
          }

      }

      OSTimeDly(TOTAL_INTERVAL_MS/2, OS_OPT_TIME_DLY, &err);
  }
}

void retrieve_data_from_buffer2_and_single_read_task_create(void) {
  RTOS_ERR err;

  OSTaskCreate(&retrieve_from_buf2_tcb,
               "RetrieveFromBuf2",
               retrieve_data_from_buffer2_and_single_read_task,
               NULL,
               RETRIEVE_DATA_FROM_BUF2_TASK_PRIO,
               &retrieve_from_buf2_stk[0],
               (RETRIEVE_DATA_FROM_BUF2_TASK_STK_SIZE / 10u),
               RETRIEVE_DATA_FROM_BUF2_TASK_STK_SIZE,
               0u,
               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR,
               &err);

}


void retrieve_data_from_buffer2_and_single_read_task(void *p_arg) {
  (void)p_arg;
  RTOS_ERR err;

  while (1) {

      // drain circular buffer and printf
      sensor_sample_t sample2; // keller_buffer_Store holds the block averaged samples

      while (sensor_data_buffer2_retrieve(&sample2)) {

          uint32_t freq = sl_sleeptimer_get_timer_frequency();                                // 32768 on EFM32GG11
          uint64_t t_sec_whole = sample2.t_ticks / freq;
          uint64_t t_sec_frac  = ((sample2.t_ticks % freq) * 1000000) / freq;

          if (system_get_single_read_flag()){

              printf("%c%03d.%03d,%03d.%02d,%02lu%06lu.%06lu,%d,%d\r\n",
                             (sample2.p_mbar<0 ? '-':' '),
                             (int)(abs(sample2.p_mbar) / 1000),
                             (int)(abs(sample2.p_mbar) % 1000),
                             (int)((sample2.t_centi * 9 / 5 + 3200) / 100),
                             (int)((sample2.t_centi * 9 / 5 + 3200) % 100),
                             (uint32_t)(t_sec_whole / 1000000),
                             (uint32_t)(t_sec_whole % 1000000),
                             (uint32_t)t_sec_frac,
                             sample2.hall,
                             sample2.ctrl_out);

              system_clear_single_read_flag(); //clear flag
          }


      }

      OSTimeDly(TOTAL_INTERVAL_MS/2, OS_OPT_TIME_DLY, &err);
  }
}

void button_stop_acqu_task_create(void) {
  RTOS_ERR err;

  OSTaskCreate(&button_stop_acqu_tcb,
               "Button stop acqu",
               button_stop_acqu_task,
               NULL,
               BUTTON_STOP_ACQU_TASK_PRIO,
               &button_stop_acqu_stk[0],
               (BUTTON_STOP_ACQU_TASK_STK_SIZE / 10u),
               BUTTON_STOP_ACQU_TASK_STK_SIZE,
               0u,
               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR,
               &err);
}

void button_stop_acqu_task(void *p_arg) {

  (void)p_arg;

  RTOS_ERR err;
  uint8_t button_press_count = 0;
  while (1) {
      if (GPIO_PinInGet(gpioPortC, 8) == 0 && mod_sd_is_open_AW()) {
          if (++button_press_count >=5){ // 5 increments of the button poll check
              button_press_count =0;
              system_request_stop_acquisition();
          }
      }
      else {
              button_press_count = 0;
      }
      OSTimeDly(50, OS_OPT_TIME_DLY, &err); // poll every 50ms
  }
}

static const char* switch_dir_to_str(switch_direction_t d) {
    switch (d) {
        case SWITCH_DIRECTION_UPCAST:   return "UPCAST";
        case SWITCH_DIRECTION_DOWNCAST: return "DOWNCAST";
        default:                        return "BOTH";
    }
}

void controller_task_create(void) {
  RTOS_ERR err;

  OSTaskCreate(&controller_tcb,
               "Controller",
               controller_task,
               NULL,
               CONTROLLER_TASK_PRIO,
               &controller_stk[0],
               (CONTROLLER_TASK_STK_SIZE / 10u),
               CONTROLLER_TASK_STK_SIZE,
               0u,
               0u,
               DEF_NULL,
               OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR,
               &err);
}

void controller_task(void *p_arg) {
  (void)p_arg;
  RTOS_ERR err;

  CMU_ClockEnable(cmuClock_GPIO, true);
  GPIO_PinModeSet(CONTROLLER_OUTPUT_PORT, CONTROLLER_OUTPUT_PIN, gpioModePushPull, 1); // starts HIGH = instrument ON (fail-safe default)

  sensor_sample_t sample3;
  int32_t latest_p_mbar = 0;
  int latest_hall = 0;
  bool bottom_turn_around_complete = 0;
  int prev_hall = -1; // not either of the hall outcomes to prevent false trigger on init

  while (1) {

      if (controller_print_config_on_resume) {
//          printf("CTRL config: on_dir=%s on_depth=%ld off_dir=%s off_depth=%ld\r\n",
                 switch_dir_to_str(system_get_switch_on_direction()),
                 (long)system_get_switch_on_depth_mbar(),
                 switch_dir_to_str(system_get_switch_off_direction()),
                 (long)system_get_switch_off_depth_mbar());
          controller_print_config_on_resume = false;
      }

      if (sensor_data_buffer3_retrieve(&sample3)){
          latest_p_mbar = sample3.p_mbar ; //update values
          latest_hall = sample3.hall;

//          printf("CTRL: p=%c%03d.%03d bar, hall=%d, ctrl_out=%d\r\n",
                 (latest_p_mbar<0 ? '-':' '),
                 (int)(abs(latest_p_mbar) / 1000),
                 (int)(abs(latest_p_mbar) % 1000),
                 latest_hall,
                 GPIO_PinOutGet(CONTROLLER_OUTPUT_PORT, CONTROLLER_OUTPUT_PIN));

          if (prev_hall != -1 && latest_hall != prev_hall){
              mod_sd_depth_turnaround_log_AW(sample3.t_ticks,latest_p_mbar);
              if (prev_hall==HALL_EFFECT_DESCENT_STATE && latest_hall ==HALL_EFFECT_ASCENT_STATE){
                  depth_bottom_turnaround_counter++;
                  printf("depth bottom turn around counter %lu\r\n",depth_bottom_turnaround_counter);
              }
          }
          prev_hall = latest_hall; // set prev hall
      }

      switch (controller_task_state) {
        case STATE_PROFILE_EST: {
//          printf("CTRL S0\r\n");
          if (depth_bottom_turnaround_counter >=3 ){ // stay in profile estimation state until we've done a few full profiles
              controller_task_state= STATE_ON_AND_WAIT;
          }
          break;
        }
        case STATE_ON_AND_WAIT: {
//          printf("CTRL S1\r\n");

          if (system_get_switch_on_direction()!=SWITCH_DIRECTION_BOTH){
              switch_direction_t off_dir = system_get_switch_off_direction();
              int32_t off_depth = system_get_switch_off_depth_mbar();

              // mark the turn around complete once actually see the direction opposite to off_dir
              if (off_dir == SWITCH_DIRECTION_DOWNCAST && latest_hall == HALL_EFFECT_ASCENT_STATE) {
                  bottom_turn_around_complete = true;
              } else if (off_dir == SWITCH_DIRECTION_UPCAST && latest_hall == HALL_EFFECT_DESCENT_STATE) {
                  bottom_turn_around_complete = true;
              }


              if (off_dir == SWITCH_DIRECTION_DOWNCAST && latest_hall == HALL_EFFECT_DESCENT_STATE && latest_p_mbar >= off_depth && bottom_turn_around_complete) {
                  controller_task_state = STATE_TURN_OFF;
              }
              else if (off_dir == SWITCH_DIRECTION_UPCAST && latest_hall == HALL_EFFECT_ASCENT_STATE && latest_p_mbar <= off_depth && bottom_turn_around_complete) {
                  controller_task_state = STATE_TURN_OFF;
              }
          }
          break;
        }
        case STATE_TURN_OFF: {
//          printf("CTRL S2\r\n");
          GPIO_PinOutClear(CONTROLLER_OUTPUT_PORT, CONTROLLER_OUTPUT_PIN); // LOW = instrument OFF
          controller_task_state = STATE_OFF_AND_WAIT;
          break;
        }
        case STATE_OFF_AND_WAIT: {
//          printf("CTRL S3\r\n");
          switch_direction_t on_dir = system_get_switch_on_direction();
          int32_t on_depth = system_get_switch_on_depth_mbar();
          if (on_dir == SWITCH_DIRECTION_DOWNCAST && ((latest_hall == HALL_EFFECT_DESCENT_STATE && latest_p_mbar >= on_depth) || latest_hall == HALL_EFFECT_ASCENT_STATE)) {
              controller_task_state = STATE_TURN_ON;
          }
          else if (on_dir == SWITCH_DIRECTION_UPCAST && ((latest_hall == HALL_EFFECT_ASCENT_STATE && latest_p_mbar <= on_depth) || latest_hall == HALL_EFFECT_DESCENT_STATE)) {
              controller_task_state = STATE_TURN_ON;
          }
          break;
        }
        case STATE_TURN_ON: {
//          printf("CTRL S4\r\n");
          GPIO_PinOutSet(CONTROLLER_OUTPUT_PORT, CONTROLLER_OUTPUT_PIN); // HIGH = instrument ON
          bottom_turn_around_complete = false; // reset, haven't seen opposite direction since this was ON
          controller_task_state = STATE_ON_AND_WAIT;
          break;
        }
      }

      OSTimeDly(1000, OS_OPT_TIME_DLY, &err);
  }
}
