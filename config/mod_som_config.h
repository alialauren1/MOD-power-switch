/*
 * mod_som_config.h
 *
 *  Created on: Dec 2, 2025
 *      Author: lwelsh
 */

#ifndef CONFIG_MOD_SOM_CONFIG_H_
#define CONFIG_MOD_SOM_CONFIG_H_



/// 2025 12 02 LW: Switches for SEGGER SystemView ///

// Enable/disable SystemView support
#define MOD_SOM_SYSVIEW_EN

// Enable/disable xQueueGenericSend/Receive messages (flooded by CLI)
#define MOD_SOM_SYSVIEW_QUEUEMSGS_EN

// Enable/disable sending stdout to SysView (NOT YET IMPLEMENTED)
//#define MOD_SOM_SYSVIEW_PRINTF_EN


/// 2025 12 09 LW: Switches for FatFS ///

// Enable/disable turning off SDCLK while the FatFS semaphore is not locked
#define MOD_SOM_FATFS_SDCLK_POWER_SAVE

// SDIO initialization clock frequency (in Hz)
#define MOD_SOM_FATFS_INIT_SDCLK_FREQ 400000

// SDIO active clock frequency (in Hz)
#define MOD_SOM_FATFS_ACTIVE_SDCLK_FREQ 1000000
//#define MOD_SOM_FATFS_ACTIVE_SDCLK_FREQ MOD_SOM_FATFS_INIT_SDCLK_FREQ


#endif /* CONFIG_MOD_SOM_CONFIG_H_ */
