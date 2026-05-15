#include "task.h"
#include "mod_sd.h" // mod sd driver
#include "cli.h"

void app_init(void){
  //create tasks
  mod_sd_create_init_task();
  keller_get_pressure_task_create();
  retrieve_pressure_from_buffer_task_create();
  button_task_create();
  cli_app_init();

}

