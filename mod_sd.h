/*
 * mod_sd.h
 *
 *  Created on: Dec 2, 2025
 *      Author: lwelsh
 */

#ifndef MOD_SD_H_
#define MOD_SD_H_

//#include "task.h"
#include "ff.h"


#define MOD_SD_INIT_STACK_SIZE 256
#define MOD_SD_INIT_PRIO       10
#define MOD_SD_CMD_STACK_SIZE  256
#define MOD_SD_CMD_PRIO        20

extern void* mod_sd_task_handle;

typedef struct{
  uint16_t val;
  uint8_t  dec;
  char     pfx;
} mod_sd_bytecount_t, *mod_sd_bytecount_ptr_t;

void mod_sd_enable_hardware();

FATFS* mod_sd_get_fs();

void mod_sd_get_bytecount(uint32_t kb_cnt, mod_sd_bytecount_ptr_t bytes);

void mod_sd_ff_encode(char* str, TCHAR* out, uint32_t len);

void mod_sd_ff_decode(TCHAR* tstr, char* out);


void mod_sd_init_task();
void mod_sd_task();

void mod_sd_create_init_task();

void mod_sd_close_and_unmount_AW(void);
bool mod_sd_write_AW(char *buf, int len);
uint8_t mod_sd_is_open_AW(void);
void mod_sd_enable_hardware_AW(void);

#endif /* MOD_SD_H_ */
