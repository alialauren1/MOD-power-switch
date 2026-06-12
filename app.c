#include "task.h"
#include "mod_sd.h" // mod sd driver
#include "cli.h"

void app_init(void){
  //create tasks
  mod_sd_create_init_task();
  get_sensor_data_task_create();
  retrieve_data_from_buffer_and_sd_store_task_create();
  button_stop_logging_task_create();
  cli_app_init();
}

