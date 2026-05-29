/***************************************************************************//**
 * @file main.c
 * @brief main() function.
 *******************************************************************************
 * # License
 * <b>Copyright 2020 Silicon Laboratories Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * The licensor of this software is Silicon Laboratories Inc. Your use of this
 * software is governed by the terms of Silicon Labs Master Software License
 * Agreement (MSLA) available at
 * www.silabs.com/about-us/legal/master-software-license-agreement. This
 * software is distributed to you in Source Code format and is governed by the
 * sections of the MSLA applicable to Source Code.
 *
 ******************************************************************************/
#include "sl_component_catalog.h"
#include "sl_system_init.h"
#include "app.h"
#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)
#include "sl_power_manager.h"
#endif
#if defined(SL_CATALOG_KERNEL_PRESENT)
#include "sl_system_kernel.h"
#else // SL_CATALOG_KERNEL_PRESENT
#include "sl_system_process_action.h"
#endif // SL_CATALOG_KERNEL_PRESENT

int main(void)
{
  sl_system_init(); // Initialize silicon labs device, system, services & protocol stacks

  app_init(); // Initialize application, runs once


#if defined(SL_CATALOG_KERNEL_PRESENT) // Adding kernel component causes this to be defined. For ex: Micrium OS Kernel

  sl_system_kernel_start(); // start kernel. tasks in app_init() start running

#else // SL_CATALOG_KERNEL_PRESENT
  while (1) {

    sl_system_process_action(); // don't remove, silicon labs components process action routine, must be called from super loop


#if defined(SL_CATALOG_POWER_MANAGER_PRESENT)

    sl_power_manager_sleep(); // lets CPU go to sleep if system allows
#endif
  }
#endif // SL_CATALOG_KERNEL_PRESENT
}
