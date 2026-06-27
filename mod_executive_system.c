/*
 * mod_executive_system.c
 *
 *  Created on: Jun 15, 2026
 *      Author: aliawolken
 */

#include "mod_executive_system.h"
#include "task.h"
#include "cli.h"
#include "mod_sd.h"
#include "os.h"
#include "rtos_err.h"
#include <stdio.h>

#define EXECUTIVE_TASK_PRIO 5u
#define EXECUTIVE_TASK_STK_SIZE 1024u
static CPU_STK executive_stk[EXECUTIVE_TASK_STK_SIZE];
static OS_TCB executive_tcb;

static system_state_t                system_state  = SYS_STARTUP;
static volatile running_mode_t       running_mode  = RUNNING_MODE_IDLE;
static run_time_variables_t          run_time_vars;

static volatile bool single_read_sensor_flag = false;

system_state_t system_get_state(void)         { return system_state; }
running_mode_t system_get_running_mode(void)  {return running_mode;}
bool system_get_single_read_flag(void)        {return single_read_sensor_flag; }

void system_request_start_acquisition(void)    { running_mode = RUNNING_MODE_AUTO_CONTROL_AND_LOG; }
void system_request_stop_acquisition(void)     { running_mode = RUNNING_MODE_IDLE; }
void system_request_single_read(void)          { single_read_sensor_flag = true; }
void system_clear_single_read_flag(void)       { single_read_sensor_flag = false; }
static bool buf2_task_is_running = false;

static void executive_task(void *p_arg) {
  (void)p_arg;
  RTOS_ERR err;
  system_state_t prev_state = SYS_STARTUP;
  bool state_entry = true;
  bool single_read_sensor_flag_copy = false;

  while (1) {
      state_entry = (system_state!= prev_state);
      prev_state = system_state;

      switch (system_state) {

        case SYS_STARTUP: {
          printf("------------------------------------------------------\r\n");
          printf("------------------------------------------------------\r\n");
          printf("S0: entered SYS_STARTUP\r\n");

          // defaults:
          running_mode = RUNNING_MODE_IDLE;
          run_time_vars.sample_rate_hz = SAMPLE_RATE_HZ_DEFAULT;
          run_time_vars.logging_on_flg = true;
          run_time_vars.controller_on_flg = false;
          single_read_sensor_flag = false;

          system_state = SYS_INIT_INFRA_TASKS;
          break;
        }

        case SYS_INIT_INFRA_TASKS: {
          printf("S1: entered SYS_INIT_INFRA_TASKS\r\n");
          cli_app_init();
          mod_sd_create_init_task();
          system_state = SYS_CONFIG;
          break;
        }

        case SYS_CONFIG: {   // reads if there is a config file, if there is it over-rides default run time variables
//          static uint32_t config_entry_tick = 0;
//          if (state_entry){
//              printf("S2: entered SYS_CONFIG\r\n");
//              config_entry_tick = sl_sleeptimer_get_tick_count();
//          }
//          if (mod_sd_is_open_AW()){
//              mod_sd_load_config_AW(&run_time_vars);
//              config_sample_rate_task(run_time_vars.sample_rate_hz); // checks if config sample rate is outside of bounds, if so resets to defaut
//
//          }
//          else {
//              uint32_t elapsed_ms = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count()-config_entry_tick);
//              if (elapsed_ms >= 3000u){ // 3000 ms unsigned integer = 3 sec
//                  printf("S2: SD not ready or avail, proceeding with defaults \r\n");
//              }
//              else {
//                  break;
//              }
//          }

          system_state = SYS_MEM;
          break;
        }

        case SYS_MEM:{
          printf("S3: entered SYS_MEM\r\n");
          system_state = SYS_INIT_ACQ_TASKS;
          break;
        }

        case SYS_INIT_ACQ_TASKS: {
          printf("S4: entered SYS_INIT_ACQ_TASKS\r\n");
          // tasks are suspended here right after their create functions !!!!
          get_sensor_data_task_create(); get_sensor_data_task_suspend_on_boot();
          retrieve_data_from_buffer_and_sd_store_task_create(); retrieve_task_suspend();          // for data logging
          retrieve_data_from_buffer2_and_single_read_task_create(); retrieve_buf2_task_suspend(); // for single reads
          // TODO: add control task
          button_stop_logging_task_create(); button_stop_logging_task_suspend();

          system_state = SYS_SELF_CHECK;
          break;
        }

        case SYS_SELF_CHECK: {
          printf("S5: entered SYS_SELF_CHECK\r\n");
          system_state = SYS_RUNNING_MODE_CHECK_AND_IDLE;
          break;
        }

        case SYS_RUNNING_MODE_CHECK_AND_IDLE: {
          if (state_entry) {printf("S6: entered SYS_RUNNING_MODE_CHECK_AND_IDLE\r\n");}
          if (running_mode == RUNNING_MODE_AUTO_CONTROL_AND_LOG){
              system_state = SYS_ACQU;
          }
          else if (single_read_sensor_flag){
              system_state = SYS_ACQU;
          }
          break;
        }

        case SYS_ACQU: {
          // TODO: fix led indicators
          // TODO: re-mount SD card when we start acquisition the second time around (after already doing acqu and then stopping, restarting again)

          if (state_entry) {
              printf("S7: entered SYS_ACQU\r\n");
              printf("logging=%d controller=%d\r\n",run_time_vars.logging_on_flg,run_time_vars.controller_on_flg);
              single_read_sensor_flag_copy = single_read_sensor_flag;
              
              sensor_request_state_reset();
              get_sensor_data_task_resume();        // start reading from sensors and store on circular buffer
              // TODO: implement button task resume, make sure button functionality now just leaves SYS_ACQU
              
              if (single_read_sensor_flag_copy){
                  retrieve_buf2_task_resume();
                  buf2_task_is_running = true;
              }
              else {
                  if (run_time_vars.logging_on_flg){
                      if (!mod_sd_is_open_AW()){
                          mod_sd_remount_and_open_AW();
                      }
                      retrieve_task_resume();           // pull from circular buf and store on sd card
                  }
                  if (run_time_vars.controller_on_flg){
                      // TODO: resume controller task
                  }
              }
          }
          

          if (running_mode == RUNNING_MODE_AUTO_CONTROL_AND_LOG && single_read_sensor_flag){ // single read while in ACQU state, resume buf2 task to print value
              if (!buf2_task_is_running){
                  retrieve_buf2_task_resume();
                  buf2_task_is_running = true;
              }
          }

          if (running_mode == RUNNING_MODE_AUTO_CONTROL_AND_LOG && !single_read_sensor_flag){ // single read while in ACQU state, resume buf2 task to print value
              if (buf2_task_is_running == true) {
                  retrieve_buf2_task_suspend();
                  buf2_task_is_running=false;
              }
          }


          // Exiting: only running_mode = idle and no pending single read
          if (running_mode == RUNNING_MODE_IDLE && !single_read_sensor_flag){

              if (!single_read_sensor_flag_copy){
                  if (run_time_vars.logging_on_flg) { retrieve_task_suspend(); }
                  if (run_time_vars.controller_on_flg) {
                      // TODO: put control task suspend here
                  }
                  // TODO: button_stop_logging_task_suspend() only if button was resumed
                  flush_sd_before_close();
                  mod_sd_close_and_unmount_AW();
              }

              get_sensor_data_task_suspend();
              reset_block_avg_data_accumulators();
              if (buf2_task_is_running) {retrieve_buf2_task_suspend(); buf2_task_is_running=false;}

              system_state = SYS_RUNNING_MODE_CHECK_AND_IDLE; // shared by both paths
          }

          break;
        }


        case SYS_ERR: {
          if (state_entry) {printf("S8: entered SYS_ERR\r\n");}
          break;
        }

      }
      OSTimeDly(10, OS_OPT_TIME_DLY, &err);
  }
}

void system_executive_task_create(void) {
    RTOS_ERR err;
    OSTaskCreate(&executive_tcb,
                 "Executive",
                 executive_task,
                 DEF_NULL,
                 EXECUTIVE_TASK_PRIO,
                 &executive_stk[0],
                 EXECUTIVE_TASK_STK_SIZE / 10u,
                 EXECUTIVE_TASK_STK_SIZE,
                 0u, 0u, DEF_NULL,
                 OS_OPT_TASK_STK_CHK | OS_OPT_TASK_STK_CLR,
                 &err);
}

