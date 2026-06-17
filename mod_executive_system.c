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

system_state_t system_get_state(void)       { return system_state; }
running_mode_t system_get_running_mode(void) {return running_mode;}
void system_request_start_acquisition(void)    { running_mode = RUNNING_MODE_AUTO_CONTROL_AND_LOG; }
void system_request_stop_acquisition(void)     { running_mode = RUNNING_MODE_IDLE; }
void system_request_single_read(void)        { single_read_sensor_flag = true; }
void system_clear_single_read_flag(void)     { single_read_sensor_flag = false; }

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
          printf("---------------------------------\r\n");
          printf("---------------------------------\r\n");
          printf("S0: entered SYS_STARTUP\r\n");

          // defaults:
          running_mode = RUNNING_MODE_IDLE;
          run_time_vars.sample_rate_hz = SAMPLE_RATE_HZ_DEFAULT;
          run_time_vars.logging_on_flg = false;
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

        case SYS_CONFIG: {
          printf("S2: entered SYS_CONFIG\r\n");
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
//          get_sensor_data_task_create();
//          retrieve_data_from_buffer_and_sd_store_task_create();
//          button_stop_logging_task_create();
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
          if (state_entry) {
              printf("S7: entered SYS_ACQU\r\n");
              printf("logging=%d controller=%d\r\n",run_time_vars.logging_on_flg,run_time_vars.controller_on_flg);
              single_read_sensor_flag_copy = single_read_sensor_flag;
              if (single_read_sensor_flag_copy){
                  // TODO: resume necessary tasks because entering this state just to print value
              }
              else {
                  // TODO: entered state upon starting recording, so need to check logging and controller flags
                  // TODO: resume get sensor data task and button task
                  if (run_time_vars.logging_on_flg){
                      // TODO: resume retrieve sensor data task
                  }
                  if (run_time_vars.controller_on_flg){
                      // TODO: resume controller task
                  }
              }
          }

          if (single_read_sensor_flag_copy){
              if (!single_read_sensor_flag){
                  // TO DO: suspend get sensor data and retrieve task
                  system_state = SYS_RUNNING_MODE_CHECK_AND_IDLE; // flag has been cleared so can move states
              }
          }
          else {
              if (running_mode == RUNNING_MODE_IDLE){
                  if (single_read_sensor_flag) {
                      // don't change states yet because still working on printing sensor values
                  }
                  else {
                      // TODO: suspend all tasks
                      // TODO: flush_sd_before_close(); reset_block_avg_data_accumulators(); mod_sd_close_and_unmount_AW();
                      system_state = SYS_RUNNING_MODE_CHECK_AND_IDLE;
                  }
              }
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

