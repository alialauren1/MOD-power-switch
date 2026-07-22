/*
 * mod_sd.c
 *
 *  Created on: Dec 2, 2025
 *      Author: lwelsh
 *              awolken (Modified April 2026)
 *
 *
 *   TODO: Keep track of what I (AW) add to this:
 *
 *   Single Lines:
 *   static FIL fp;
 *   static void mod_sd_open_AW(void);
 *   mod_sd_open_AW(); // inside the function mod_sd_init_task()
 *
 *   // the mutex code inside mod_sd_create_init_task()
 *
 *   GPIO_PinModeSet(gpioPortH, 11, gpioModePushPull,1); // LED0 made green, starts OFF (active low so 1 is off)
 *   GPIO_PinModeSet(gpioPortH,10,gpioModeInputPull,1);
 *
 *   Created the Functions:
 *   mod_sd_open_AW(void)
 *   mod_sd_close_and_unmount_AW(void)
 *   mod_sd_write_AW()
 *
 */

//#include "FreeRTOS.h"
#include "os.h"
#include "sl_sleeptimer.h"
#include "em_gpio.h"
#include "em_cmu.h"
#include "ff.h"
#include "diskio.h"
#include "string.h"
//#include "task.h"
//#include "semphr.h"
#include <stdio.h>
#include "app.h"
#include "mod_sd.h"
//#include <stdalign.h>
#include "microsd.h"

#define SD_FILE_MAX_SIZE (5*1024*1024) // Keep units in bytes

//TaskHandle_t mod_sd_init_task_handle;
//TaskHandle_t mod_sd_cmd_task_handle;
OS_TCB mod_sd_init_task_handle;
static CPU_STK mod_sd_init_task_stk[MOD_SD_INIT_STACK_SIZE];
//TaskHandle_t mod_sd_task_2_handle;
//SemaphoreHandle_t sync_sem;
OS_SEM sync_sem;


static volatile FATFS fat_fs;

static FIL fp;  // AW added
static FIL cfg_fp;  // config file handle, static to avoid putting large FIL struct on stack
static OS_MUTEX sd_mutex;         // AW, protecting fp so write and close cant overlap
static volatile uint8_t sd_file_open = 0; // AW, 0 for when not safe to write, 1 for when is safe to write
static volatile bool sd_init_done = false;
static volatile uint8_t sd_write_prev = 0;
static void mod_sd_open_sensor_log_AW(void); // AW added, is a forward declaration
void mod_sd_seed_rtc_AW(void);
static char name_buf[16];                  // char array for building filename string "data_xxxx.csv"

static RTOS_ERR err;
//static FIL fp;
//static FIL fp_2;
//
//static char test_msg[] = "sup fella";
//static TCHAR test_tch[16];
//static TCHAR test_tch_2[16];
//
//#define TEST_BUF_LEN 1024 * 8
//
//static char test_buf[TEST_BUF_LEN] __attribute__ ((aligned (sizeof(uint32_t))));
//static char comp_buf[TEST_BUF_LEN];
//
//#define FMT_BUF_LEN 512 * 16
//static BYTE fmt_buf[FMT_BUF_LEN];


void mod_sd_enable_hardware()
{
  CMU_ClockEnable(cmuClock_GPIO, true);


  // Soldered sdCard slot
//  GPIO_PinModeSet(gpioPortD, 6u, gpioModePushPull, 1); //SD_EN
//  GPIO_PinOutSet(gpioPortD, 6u);
//

  GPIO_PinModeSet(gpioPortE, 7, gpioModePushPullAlternate, 1);  // GG11 STK3701 SDIO_PWR_ENABLE

  sl_sleeptimer_delay_millisecond(10);

  //ALB TODO Make CD works

  // 2024 12 12 LW: Changing to CD LOC0 (PF8)
  GPIO_PinModeSet(gpioPortB, 10, gpioModeInput, 1);              // SDIO_CD
//  GPIO_PinModeSet(gpioPortF, 8, gpioModeInput, 1);              // SDIO_CD
  GPIO_PinModeSet(gpioPortE, 15, gpioModePushPullAlternate, 0); // SDIO_CMD
  GPIO_PinModeSet(gpioPortE, 14, gpioModePushPullAlternate, 1); // SDIO_CLK
  GPIO_PinModeSet(gpioPortA, 0, gpioModePushPullAlternate, 1);  // SDIO_DAT0
  GPIO_PinModeSet(gpioPortA, 1, gpioModePushPullAlternate, 1);  // SDIO_DAT1
  GPIO_PinModeSet(gpioPortA, 2, gpioModePushPullAlternate, 1);  // SDIO_DAT2
  GPIO_PinModeSet(gpioPortA, 3, gpioModePushPullAlternate, 1);  // SDIO_DAT3

//  GPIO_PinModeSet(gpioPortH, 11, gpioModePushPull,1); // LED0 made green, starts OFF (active low so 1 is off)
//  GPIO_PinModeSet(gpioPortC,8,gpioModeInputPull,1); // 1 sets pull-up so the pin reads high at rest, pressing button pulls it low

  sl_sleeptimer_delay_millisecond(1);

}


FATFS* mod_sd_get_fs()
{
  return &fat_fs;
}

void mod_sd_get_bytecount(uint32_t kb_cnt, mod_sd_bytecount_ptr_t bytes)
{

  // ">> 1" is equivalent to "multiply by 512 to get bytes, then divide by 1024 to get kilobytes"
  float val = 0;
  float dec = 0;

//  fs_uspc_dec = (float)(fs_uspc - ((uint8_t)fs_uspc))*100;
  uint16_t kb = 1000;

  if(kb_cnt < kb)
  {
    bytes->pfx = 'K';
    bytes->val = kb_cnt;
    bytes->dec = 0;
  }
  else
  {
    if(kb_cnt < kb * kb)
    {
      bytes->pfx = 'M';
      val = (float)kb_cnt / kb;
    }
    else if(kb_cnt < kb * kb * kb)
    {
      bytes->pfx = 'G';
      val = (float)kb_cnt / (kb * kb);
    }
    else
    {
      bytes->pfx = 'T';
      val = (float)kb_cnt / (kb * kb * kb);
    }

    bytes->val = (uint32_t) val;
    dec = (float)(val - ((uint32_t)val))*100;
    bytes->dec = (uint8_t) dec;
  }
}


// 2025 12 05 LW: Function for converting strings (char) to UTF-8 (TCHAR) for path names
void mod_sd_ff_encode(char* str, TCHAR* out, uint32_t len)
{
  uint32_t i;
  for(i = 0; i < len; i++)
  {
      out[i] = ff_convert(str[i], 1);
  }
  out[i] = ff_convert('\0', 1);
}

// 2025 12 05 LW: Function for converting UTF-8 (TCHAR) to string (char)
void mod_sd_ff_decode(TCHAR* tstr, char* out)
{
  int i = 0;
  TCHAR t = 0xff;
  while(t != 0)
  {
      t = tstr[i];
      out[i++] = (char)t;
  }
}

// 2026 02 20 LW: Task to initialize the SD card on startup
// 2026 04 28 AW: File creation and writing
void mod_sd_init_task()
{
  volatile FRESULT res;

  mod_sd_enable_hardware_AW();
  mod_sd_seed_rtc_AW();

  MICROSD_Init();

//  SEGGER_SYSVIEW_WarnfHost("mount");
  res = f_mount(&fat_fs,(TCHAR*)"", 1);

  if(res == (FRESULT)RES_OK)
  {
      printf("FATfs mount success\r\n");
      mod_sd_open_sensor_log_AW();
  }
  else
  {
      printf("Unable to mount FAT fs.\r\n");
  }


//  xTaskNotifyGive(mod_som_init_task_handle);

  sd_init_done = true;

  for( ;; )
  {
//      vTaskDelete(NULL);
      OSTaskSuspend(&mod_sd_init_task_handle,
                &err);
  }

}



void mod_sd_create_init_task()
{
  // TODO: Review how/when this semaphore should be locked/unlocked
//  sync_sem = xSemaphoreCreateBinary();
//  xSemaphoreGive(sync_sem);
//
//  xTaskCreate(mod_sd_init_task,
//              "mod_sd_init",
//              configMINIMAL_STACK_SIZE,
//              NULL,
//              18,
//              &mod_sd_init_task_handle);

  RTOS_ERR err;

  OSMutexCreate(&sd_mutex,"SD Mutex", &err);            // create mutex before any task can call to write or close
  EFM_ASSERT((RTOS_ERR_CODE_GET(err)==RTOS_ERR_NONE));  // if the mutex creation fails, halt in debug

  OSTaskCreate(&mod_sd_init_task_handle,
               "mod_sd_init",
               mod_sd_init_task,
               DEF_NULL,
               MOD_SD_INIT_PRIO,
               &mod_sd_init_task_stk[0],
               (MOD_SD_INIT_STACK_SIZE / 10u),
               MOD_SD_INIT_STACK_SIZE,
               0u,
               0u,
               DEF_NULL,
               (OS_OPT_TASK_STK_CLR),
               &err);

  EFM_ASSERT((RTOS_ERR_CODE_GET(err) == RTOS_ERR_NONE));


}

// AW added the following task:
static void mod_sd_open_sensor_log_AW(void){
  UINT bw;                                   // bw (bytes written) so f_write fills this in after the write
  TCHAR file_name[16];                       // array for the UTF-16 encoded file path
  FILINFO fno;                                // FatFS file info struct

  // find placement to create new file, binary search algorithm
  int lo = 0, hi = 9999, file_num; // lo & hi define the search window, file_num holds the result
  while (lo < hi) {
      int mid = lo + (hi - lo) / 2;
      snprintf(name_buf, sizeof(name_buf), "data_%04d.csv", mid); // build filename string in RAM, %04d zero-pads the number
      mod_sd_ff_encode(name_buf, file_name, strlen(name_buf)); // encode filename for FatFS: converting string into format for FatFS storing it in file_name
      if (f_stat(file_name, &fno) == FR_NO_FILE) hi = mid; // file missing: first unused slot is at mid or below, shrink top of window
      else lo = mid + 1;                                     // file exists: first unused slot is above mid, shrink bottom of window
  }
  file_num = lo;

  if(file_num>9999){
      printf("SD error:max file count has been reached \r\n");
      GPIO_PinOutClear(gpioPortH,10); // turn on led to RED: LED is active low (driving low turns it on)
      return;
  }

  // loop exited with name_buf set to last mid, not file_num — rebuild with the correct number
  snprintf(name_buf, sizeof(name_buf), "data_%04d.csv", file_num);
  mod_sd_ff_encode(name_buf, file_name, strlen(name_buf));


  FRESULT fres = f_open(&fp, file_name, FA_CREATE_NEW | FA_WRITE); // create file

  if(fres==FR_OK){
      GPIO_PinOutSet(gpioPortH, 10);    // clear red error LED on successful open
      GPIO_PinOutClear(gpioPortH,11); // turn on led to GREEN: LED is active low (driving low turns it on)
      sd_file_open = 1;               // set flag s.t. fp is now valid and writing is allowed
      f_write(&fp,"MOD LAB: Keller pressure sensor & Hall Effect sensor data, Controller Output\r\n",sizeof("MOD LAB: Keller pressure sensor & Hall Effect sensor data, Controller Output\r\n") - 1,&bw); // writes bytes to the file, bw receives the actual bytes written
      f_write(&fp, "Pressure [bar],Temperature [F],time [sec],hall, controller output\r\n", sizeof("Pressure [bar],Temperature [F],time [sec], hall, controller output\r\n") - 1, &bw);
      printf("File created: %s \r\n", name_buf);
  }
  else {
      printf("File open has failed: %d\r\n",fres);
      GPIO_PinOutSet(gpioPortH, 11);  // turn off led to GREEN — no longer accurate to claim "recording"
      GPIO_PinOutClear(gpioPortH,10); // turn on led to RED: LED is active low (driving low turns it on)
  }
}

bool mod_sd_remount_and_open_AW(void){
  FRESULT res = f_mount(&fat_fs, (TCHAR*)"", 1);
  if (res != FR_OK) {
      printf("Remount failed: %d\r\n", res);
      return false;
  }
  printf("Remount success\r\n");
  mod_sd_open_sensor_log_AW();
  if (!mod_sd_is_open_AW()) {
      printf("File open failed after remount.\r\n");
      return false;
  }
  return true;
}

uint8_t mod_sd_is_open_AW(void) { return sd_file_open; }
bool mod_sd_init_done_AW(void) { return sd_init_done; }

void mod_sd_close_and_unmount_AW(void) {
    RTOS_ERR err;
    OSMutexPend(&sd_mutex, 0, OS_OPT_PEND_BLOCKING, NULL, &err); // acquire mutex

    if (!sd_file_open) {
        OSMutexPost(&sd_mutex, OS_OPT_POST_NONE, &err); // protecting fp so write and close cant overlap
        printf("SD card already unmounted.\r\n");
        return;
    }

    sd_file_open = 0; // clear flag now that mutex is acquired
    sd_write_prev = 0; // clear so next first successful write will trigger the LED to turn on
    OSMutexPost(&sd_mutex, OS_OPT_POST_NONE, &err); // release the lock
    f_close(&fp);
    f_mount(NULL, (TCHAR*)"", 0); // unmount file system
    GPIO_PinOutSet(gpioPortH, 11); // turn off LED
    GPIO_PinOutSet(gpioPortH, 15); // turn off LED
    printf("SD card safe to remove.\r\n");
}

bool mod_sd_write_AW(char *buf, int len){
  RTOS_ERR err;
  UINT bw;
  bool successful_write = false;
  OSMutexPend(&sd_mutex,0,OS_OPT_PEND_BLOCKING,NULL,&err);  // acquire sd_mutex lock before touching fp, protecting fp so write and close cant overlap

  if(sd_file_open){

      if ((int)f_size(&fp) + len >= SD_FILE_MAX_SIZE) {
          f_close(&fp);
          sd_file_open = 0;
          mod_sd_open_sensor_log_AW(); // find next file name and open it
          printf("File size limit reached, opened: %s\r\n", name_buf);
      }

      FRESULT fres = f_write(&fp, buf, len, &bw); // only write to sd if fp is valid
      if (fres != FR_OK){
          sd_write_prev = 0;
          GPIO_PinOutSet(gpioPortH, 15); // turn LED off, only on transition from ok to failed
          printf("SD write error: %d\r\n", fres);
          f_close(&fp);        // close corrupted handle so subsequent writes don't keep failing
          sd_file_open = 0;    // clear flag to match closed state
          mod_sd_open_sensor_log_AW();    // open a fresh file so recovery is automatic
      }
      else {
          FRESULT fsync_res = f_sync(&fp);            // flush to SD card to protect against power loss before unmount
          if (fsync_res != FR_OK){
              sd_write_prev=0;
              GPIO_PinOutSet(gpioPortH, 15);
              printf("SD sync error: %d\r\n", fsync_res);
              f_close(&fp);        // close corrupted handle so subsequent writes don't keep failing
              sd_file_open = 0;    // clear flag to match closed state
              mod_sd_open_sensor_log_AW();    // open a fresh file so recovery is automatic
          }
          else { // LED handling
              if(!sd_write_prev){
                    sd_write_prev=1;
                    GPIO_PinOutClear(gpioPortH, 15);  // turn LED on, only on transition from failed to ok
              }
              successful_write = true;
          }
      }
  }
  OSMutexPost(&sd_mutex,OS_OPT_POST_NONE,&err);             // release sd_mutex lock, protecting fp so write and close cant overlap
  return successful_write;
}

void mod_sd_enable_hardware_AW(void) {
  GPIO_PinModeSet(gpioPortH, 11, gpioModePushPull, 1); // LED, active low, starts OFF
  GPIO_PinModeSet(gpioPortH, 10, gpioModePushPull, 1); // LED, active low, starts OFF
  GPIO_PinModeSet(gpioPortH, 15, gpioModePushPull, 1); // LED, active low, starts OFF
  GPIO_PinModeSet(gpioPortC, 8, gpioModeInputPull, 1); // BTN0, pull-up so reads high at rest
}

void mod_sd_seed_rtc_AW(void){
  sl_sleeptimer_date_t date;
  sl_sleeptimer_build_datetime(&date,1980,MONTH_JANUARY,1,0,0,0,0);
  sl_sleeptimer_set_datetime(&date);  // seed the MCU clock with the default time
}

const char* mod_sd_get_filename_AW(void) {return name_buf;} // returns name of file open on sd card

void mod_sd_log_set_time_AW(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec){
  FIL log_time_fp;
  UINT bw;
  char log_time_buf[64];
  uint32_t ticks = sl_sleeptimer_get_tick_count(); // tick count at moment set_time was installed

  TCHAR log_time_file_name[16];
  mod_sd_ff_encode("time_log.csv",log_time_file_name, strlen("time_log.csv"));
  FRESULT fres = f_open(&log_time_fp,log_time_file_name, FA_OPEN_ALWAYS | FA_WRITE); // create time log file if doesn't already exist
  if (fres==FR_OK){
      if (f_size(&log_time_fp)==0){ // if file header doesn't already exist, make it
          f_write(&log_time_fp, "data_file,real_world_time,ticks_at_set_time\r\n",strlen("data_file,real_world_time,ticks_at_set_time\r\n"), &bw);
      }

      f_lseek(&log_time_fp,f_size(&log_time_fp)); // seek to end so new entries are appended and not re-written

      snprintf(log_time_buf,sizeof(log_time_buf),"%s,%04u-%02u-%02u %02u:%02u:%02u,%lu\r\n",mod_sd_get_filename_AW(), year, month, day, hour, min, sec, ticks);
      f_write(&log_time_fp,log_time_buf,strlen(log_time_buf),&bw);
      f_close(&log_time_fp);
  }
}

static const char* switch_direction_to_str(switch_direction_t d) {
    switch (d) {
        case SWITCH_DIRECTION_UPCAST:   return "UPCAST";
        case SWITCH_DIRECTION_DOWNCAST: return "DOWNCAST";
        default:                        return "BOTH";
    }
}

void mod_sd_load_config_AW(run_time_variables_t *cfg){
  char   cfg_buf[200];
  UINT   br;
  TCHAR  cfg_name[16];

  mod_sd_ff_encode("config.cfg", cfg_name, strlen("config.cfg"));

  FRESULT res = f_open(&cfg_fp, cfg_name, FA_READ);

  if (res == FR_NO_FILE){
      // config.cfg doesn't exist yet, create it with defaults
      res = f_open(&cfg_fp, cfg_name, FA_WRITE | FA_CREATE_NEW);
       if (res == FR_OK) {
           UINT bw;
           char line[200];
           int len = snprintf(line, sizeof(line),
                    "sample_rate_hz=%u\r\nlogging_on_flg=%d\r\ncontroller_on_flg=%d\r\n"
                    "switch_on_direction=%s\r\nswitch_on_depth_mbar=%ld\r\nswitch_off_direction=%s\r\nswitch_off_depth_mbar=%ld\r\n",
                    cfg->sample_rate_hz,
                    (int)cfg->logging_on_flg,
                    (int)cfg->controller_on_flg,
                    switch_direction_to_str(cfg->switch_on_direction),
                    (long)cfg->switch_on_depth_mbar,
                    switch_direction_to_str(cfg->switch_off_direction),
                    (long)cfg->switch_off_depth_mbar);
           f_write(&cfg_fp, line, len, &bw);
           f_close(&cfg_fp);
           printf("config.cfg created with defaults\r\n");
       } else {
           printf("config.cfg create failed: %d\r\n", res);
       }
  }
  else if (res == FR_OK) {
      f_read(&cfg_fp, cfg_buf, sizeof(cfg_buf) - 1, &br);
      cfg_buf[br] = '\0';  // null-terminate so strtok and sscanf treat it as a valid C string
      f_close(&cfg_fp);

      unsigned int parsed_hz = -1;
      int parsed_logging = -1;
      int parsed_controller = -1;
      long parsed_on_depth_mbar = -1;
      long parsed_off_depth_mbar = -1;
      char switch_on_direction_str[16] = "";
      char switch_off_direction_str[16] = "";

      // parse line by line — overwrites in memory the fields that exist in the file,
      // leaving the rest at the defaults set in SYS_STARTUP
      char *line = strtok(cfg_buf, "\r\n");
      while (line != NULL) {
          if (sscanf(line, "sample_rate_hz=%u", &parsed_hz) == 1) {
              if (parsed_hz >= 1 && parsed_hz <= 100) cfg->sample_rate_hz = parsed_hz;
              else {
                  cfg->sample_rate_hz = SAMPLE_RATE_HZ_DEFAULT;
                  printf("config.cfg: sample_rate_hz out of range, using default\r\n");
              }
          }
          if (sscanf(line, "logging_on_flg=%d", &parsed_logging) == 1) {
              if (parsed_logging == 0 || parsed_logging == 1) cfg->logging_on_flg = (bool)parsed_logging;
          }
          if (sscanf(line, "controller_on_flg=%d", &parsed_controller) == 1) {
              if (parsed_controller == 0 || parsed_controller == 1) cfg->controller_on_flg = (bool)parsed_controller;
          }
          if (sscanf(line, "switch_on_direction=%15s", switch_on_direction_str) == 1) {
              if (strcmp(switch_on_direction_str, "UPCAST") == 0) cfg->switch_on_direction = SWITCH_DIRECTION_UPCAST;
              else if (strcmp(switch_on_direction_str, "DOWNCAST") == 0) cfg->switch_on_direction = SWITCH_DIRECTION_DOWNCAST;
              else cfg->switch_on_direction = SWITCH_DIRECTION_BOTH;
          }
          if (sscanf(line, "switch_on_depth_mbar=%ld", &parsed_on_depth_mbar) == 1) {
              cfg->switch_on_depth_mbar = (int32_t)parsed_on_depth_mbar;
          }
          if (sscanf(line, "switch_off_direction=%15s", switch_off_direction_str) == 1) {
              if (strcmp(switch_off_direction_str, "UPCAST") == 0) cfg->switch_off_direction = SWITCH_DIRECTION_UPCAST;
              else if (strcmp(switch_off_direction_str, "DOWNCAST") == 0) cfg->switch_off_direction = SWITCH_DIRECTION_DOWNCAST;
              else cfg->switch_off_direction = SWITCH_DIRECTION_BOTH;
          }
          if (sscanf(line, "switch_off_depth_mbar=%ld", &parsed_off_depth_mbar) == 1) {
              cfg->switch_off_depth_mbar = (int32_t)parsed_off_depth_mbar;
          }
          line = strtok(NULL, "\r\n");  // advance to next line; NULL continues from last strtok position
      }

      if (parsed_hz == -1 || parsed_logging == -1 || parsed_controller == -1
          || switch_on_direction_str[0] == '\0' || parsed_on_depth_mbar == -1
          || switch_off_direction_str[0] == '\0' || parsed_off_depth_mbar == -1) {
          FRESULT rewrite_res = f_open(&cfg_fp, cfg_name, FA_WRITE | FA_CREATE_ALWAYS);
          if (rewrite_res == FR_OK) {
              UINT bw;
              char out_line[200];
              int len = snprintf(out_line, sizeof(out_line),
                                 "sample_rate_hz=%u\r\nlogging_on_flg=%d\r\ncontroller_on_flg=%d\r\n"
                                 "switch_on_direction=%s\r\nswitch_on_depth_mbar=%ld\r\nswitch_off_direction=%s\r\nswitch_off_depth_mbar=%ld\r\n",
                                 cfg->sample_rate_hz,
                                 (int)cfg->logging_on_flg,
                                 (int)cfg->controller_on_flg,
                                 switch_direction_to_str(cfg->switch_on_direction),
                                 (long)cfg->switch_on_depth_mbar,
                                 switch_direction_to_str(cfg->switch_off_direction),
                                 (long)cfg->switch_off_depth_mbar);
              f_write(&cfg_fp, out_line, len, &bw);
              f_close(&cfg_fp);
              printf("config.cfg updated with missing fields\r\n");
          } else {
              printf("config.cfg update failed: %d\r\n", rewrite_res);
          }
      }
      printf("config.cfg loaded\r\n");
  }
  else {
      printf("config.cfg open error: %d, using defaults\r\n", res);
  }

}

void mod_sd_depth_turnaround_log_AW(uint64_t ticks, int32_t turnaround_depth){
  FIL log_depth_fp;
  UINT bw;
  char log_depth_buf[64];

  TCHAR log_depth_file_name[25]; // 24 TCHAR's, +1 for null
  mod_sd_ff_encode("depth_turnaround_log.csv",log_depth_file_name,strlen("depth_turnaround_log.csv"));
  FRESULT fres=f_open(&log_depth_fp,log_depth_file_name,FA_OPEN_ALWAYS|FA_WRITE); // opens if exists, creates depth log file if one doesn't exist already

  if (fres==FR_OK){
      if(f_size(&log_depth_fp)==0){ // if file header doesn't already exist, make it
          f_write(&log_depth_fp,"ticks_time_at_depth,turnaround_depth\r\n",strlen("ticks_time_at_depth,turnaround_depth\r\n"),&bw);
      }

      f_lseek(&log_depth_fp,f_size(&log_depth_fp)); // seek to end so new entries appended and not re-written

      snprintf(log_depth_buf,sizeof(log_depth_buf),"%llu,%d\r\n",ticks,turnaround_depth);
      f_write(&log_depth_fp,log_depth_buf,strlen(log_depth_buf),&bw);
      f_close(&log_depth_fp);
  }

}

