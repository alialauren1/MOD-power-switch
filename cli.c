/***************************************************************************//**
 * @file
 * @brief cli micrium os kernel examples functions
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


// AW: commented out tasks not using and that freeze when collecting data if they are used
// AW: in echo_int function, added context for number range

#include <string.h>
#include <stdio.h>
#include "cli.h"
#include "sl_cli.h"
#include "sl_cli_instances.h"
#include "sl_cli_arguments.h"
#include "sl_cli_handles.h"
#include "os.h"

#include "mod_sd.h"
#include "sl_sleeptimer.h"

/*******************************************************************************
 *******************************   DEFINES   ***********************************
 ******************************************************************************/

#define CLI_DEMO_TASK_STACK_SIZE     256
#define CLI_DEMO_TASK_PRIO            15

/*******************************************************************************
 *********************   LOCAL FUNCTION PROTOTYPES   ***************************
 ******************************************************************************/

void echo_str(sl_cli_command_arg_t *arguments);
void echo_int(sl_cli_command_arg_t *arguments);
//void sd_format_cmd(sl_cli_command_arg_t *arguments);
//void sd_ls_cmd(sl_cli_command_arg_t *arguments);
//void sd_rm_cmd(sl_cli_command_arg_t *arguments);
//void sd_write_cmd(sl_cli_command_arg_t *arguments);
void sd_read_cmd(sl_cli_command_arg_t *arguments);
//void sd_info_cmd(sl_cli_command_arg_t *arguments);

void sd_close_and_unmount_cmd(sl_cli_command_arg_t *arguments);
void sd_set_time_cmd(sl_cli_command_arg_t *arguments);
void get_time_cmd(sl_cli_command_arg_t *arguments);

/*******************************************************************************
 ***************************  LOCAL VARIABLES   ********************************
 ******************************************************************************/

static const sl_cli_command_info_t cmd__echostr = \
  SL_CLI_COMMAND(echo_str,
                 "echoes string arguments to the output",
                 "Just a string...",
                 { SL_CLI_ARG_WILDCARD, SL_CLI_ARG_END, });

static const sl_cli_command_info_t cmd__echoint = \
  SL_CLI_COMMAND(echo_int,
                 "echoes integer arguments to the output",
                 "Just a number (8 bit unsigned int: range -128 to 127)...",
                 { SL_CLI_ARG_INT8, SL_CLI_ARG_ADDITIONAL, SL_CLI_ARG_END, });

//static const sl_cli_command_info_t cmd__sd_format = \
//  SL_CLI_COMMAND(sd_format_cmd,
//                 "format the SD card",
//                 "",
//                 { SL_CLI_ARG_END, });

//static const sl_cli_command_info_t cmd__sd_ls = \
//  SL_CLI_COMMAND(sd_ls_cmd,
//                 "list the files on the SD card - only do if card is unmounted & closed",
//                 "",
//                 { SL_CLI_ARG_END, });

//static const sl_cli_command_info_t cmd__sd_rm = \
//  SL_CLI_COMMAND(sd_rm_cmd,
//                 "delete a file from the SD card",
//                 "filename",
//                 { SL_CLI_ARG_WILDCARD, SL_CLI_ARG_END, });
//
//static const sl_cli_command_info_t cmd__sd_write = \
//  SL_CLI_COMMAND(sd_write_cmd,
//                 "store a string in a new file on the SD card",
//                 "filename"SL_CLI_UNIT_SEPARATOR "string to store",
//                 { SL_CLI_ARG_WILDCARD, SL_CLI_ARG_WILDCARD, SL_CLI_ARG_END, });

static const sl_cli_command_info_t cmd__sd_read = \
  SL_CLI_COMMAND(sd_read_cmd,
                 "print the contents of a file on the SD card",
                 "filename",
                 { SL_CLI_ARG_WILDCARD, SL_CLI_ARG_END, });

//static const sl_cli_command_info_t cmd__sd_info = \
//  SL_CLI_COMMAND(sd_info_cmd,
//                 "get info about the SD card",
//                 "",
//                 { SL_CLI_ARG_END, });

static const sl_cli_command_info_t cmd__sd_close_unmount = \
  SL_CLI_COMMAND(sd_close_and_unmount_cmd,
                 "close and unmount the SD card",
                 "",
                 { SL_CLI_ARG_END, });

static const sl_cli_command_info_t cmd__sd_set_time = \
  SL_CLI_COMMAND(sd_set_time_cmd,
                 "set time for current sd card data run in the form of:",
                 "year(YYYY)" SL_CLI_UNIT_SEPARATOR "month(1-12)" SL_CLI_UNIT_SEPARATOR "day" SL_CLI_UNIT_SEPARATOR "hour" SL_CLI_UNIT_SEPARATOR "min" SL_CLI_UNIT_SEPARATOR "sec",
                 { SL_CLI_ARG_UINT16, SL_CLI_ARG_UINT8, SL_CLI_ARG_UINT8, SL_CLI_ARG_UINT8, SL_CLI_ARG_UINT8, SL_CLI_ARG_UINT8, SL_CLI_ARG_END, });

static const sl_cli_command_info_t cmd__get_time = \
  SL_CLI_COMMAND(get_time_cmd,
                 "get current system time",
                 " ",
                 { SL_CLI_ARG_END });


static sl_cli_command_entry_t a_table[] = {
  { "echo_str", &cmd__echostr, false },
  { "echo_int", &cmd__echoint, false },
//  { "sd_format", &cmd__sd_format, false },
//  { "sd_ls", &cmd__sd_ls, false },
//  { "sd_rm", &cmd__sd_rm, false },
//  { "sd_write", &cmd__sd_write, false },
  { "sd_read", &cmd__sd_read, false },
//  { "sd_info", &cmd__sd_info, false },
  { "sd_close_unmount", &cmd__sd_close_unmount, false },
  { "set_time", &cmd__sd_set_time, false },
  { "get_time", &cmd__get_time, false },
  { NULL, NULL, false },
};

static sl_cli_command_group_t a_group = {
  { NULL },
  false,
  a_table
};

static bool confirm = false;

/*******************************************************************************
 *************************  EXPORTED VARIABLES   *******************************
 ******************************************************************************/

sl_cli_command_group_t *command_group = &a_group;

/*******************************************************************************
 *************************   LOCAL FUNCTIONS   *********************************
 ******************************************************************************/

/***************************************************************************//**
 * Callback for echo_str
 *
 * This function is used as a callback when the echo_str command is called
 * in the cli. It simply echoes back all the arguments provided as strings.
 ******************************************************************************/
void echo_str(sl_cli_command_arg_t *arguments)
{
  char *ptr_string;

  printf("<<echo_str command>>\r\n");

  // Read all the arguments provided as strings and print them back
  for (int i = 0; i < sl_cli_get_argument_count(arguments); i++) {
    ptr_string = sl_cli_get_argument_string(arguments, i);

    printf("%s\r\n", ptr_string);
  }
}

/***************************************************************************//**
 * Callback for echo_int
 *
 * This function is used as a callback when the echo_int command is called
 * in the cli. It simply echoes back all the arguments provided as integers.
 ******************************************************************************/
void echo_int(sl_cli_command_arg_t *arguments)
{
  int8_t argument_value;

  printf("<<echo_int command>>\r\n");

  // Read all the arguments provided as integers and print them back
  for (int i = 0; i < sl_cli_get_argument_count(arguments); i++) {
    argument_value = sl_cli_get_argument_int8(arguments, i);

    printf("%i\r\n", argument_value);
  }
}

/***************************************************************************/
//
//void sd_format_confirm(char *arg_str, void *user)
//{
//  confirm = (arg_str[0] == 'y') || (arg_str[0] == 'Y');
//
//  if(confirm)
//  {
//    printf("I would if I could, bud.\r\n");
//  }
//  else
//  {
//    printf("Ok nevermind then\r\n");
//  }
//
//  sl_cli_redirect_command(sl_cli_default_handle, NULL, NULL, NULL);
//
//}

/***************************************************************************//**
 * Callback for sd_format
 *
 * The command is used to format the SD card.
 ******************************************************************************/
//void sd_format_cmd(sl_cli_command_arg_t *arguments)
//{
//  printf("Still need to implement!\r\n");
//  sl_cli_redirect_command(sl_cli_default_handle, (sl_cli_command_function_t)sd_format_confirm, "FORMATTING WILL DELETE ALL DATA. Are you sure? (y/n) ", NULL);
//}

/***************************************************************************//**
 * Callback for sd_ls
 *
 * The command is used to list the contents of the SD card.
 ******************************************************************************/
//void sd_ls_cmd(sl_cli_command_arg_t *arguments)
//{
//  FRESULT res;
//      DIR dir;
//      FILINFO fno;
//      int nfile, ndir;
//      char *path = "/";
//      TCHAR buf[256];
//      char fnbuf[64];
//      mod_sd_ff_encode(path, buf, strlen(path));
//      path = buf;
//
//      res = f_opendir(&dir, path);                   /* Open the directory */
//      if (res == FR_OK) {
//          nfile = ndir = 0;
//          for (;;) {
//              res = f_readdir(&dir, &fno);           /* Read a directory item */
//              if (fno.fname[0] == 0 || res != FR_OK) break;          /* Error or end of dir */
//              mod_sd_ff_decode(fno.fname, fnbuf);
//              if (fno.fattrib & AM_DIR) {            /* It is a directory */
//                  printf("   <DIR>   %s\r\n", fnbuf);
//                  ndir++;
//              } else {                               /* It is a file */
//                  printf("%10lu %s\r\n", (uint32_t)fno.fsize, fnbuf);
//                  nfile++;
//              }
//          }
//          f_closedir(&dir);
//          printf("%d dirs, %d files.\r\n", ndir, nfile);
//      } else {
//          printf("Failed to open \"%s\". (%u)\r\n", path, res);
//      }
////      return res;
//}

/***************************************************************************//**
 * Callback for sd_rm
 *
 * The command is used to delete a file from the SD card.
 ******************************************************************************/
//void sd_rm_cmd(sl_cli_command_arg_t *arguments)
//{
//  printf("Still need to implement!\r\n");
//}

/***************************************************************************//**
 * Callback for sd_write
 *
 * The command is used to write a file to the SD card.
 ******************************************************************************/
//void sd_write_cmd(sl_cli_command_arg_t *arguments)
//{
//  printf("Still need to implement!\r\n");
//}

/***************************************************************************//**
 * Callback for sd_read
 *
 * The command is used to read a file on the SD card.
 ******************************************************************************/
void sd_read_cmd(sl_cli_command_arg_t *arguments)
{
  char buf[512];
  TCHAR fnbuf[255];
  char *fname;
  FRESULT res;
  FIL file;
  uint16_t rcnt = 0;

  // Get the filename given by the user
  if (sl_cli_get_argument_count(arguments) < 1) {
    // TODO: Find a way to make the command name and arg name printing modular, not hardcoded
    printf("usage: sd_read [filename]\r\n");
    return;
  }
  fname = sl_cli_get_argument_string(arguments, 0);
  mod_sd_ff_encode(fname, fnbuf, strlen(fname));

  // Attempt to open a file with the given filename
  res = f_open(&file, fnbuf, FA_READ);
  if (res) {
    printf("File not found: \"%s\"\r\n", fname);
  }

  // Read the file in chunks, and print each chunk
  // TODO: Split up into read task and print task for more efficient operation
  for(;;)
  {
    res = f_read(&file, buf, sizeof buf, &rcnt);
    fwrite(buf, sizeof(char), rcnt, stdout);
    if(rcnt < sizeof buf) break;
  }

  // Close the file
  f_close(&file);


}

/***************************************************************************//**
 * Callback for sd_info
 *
 * The command is used to get info about the SD card.
 ******************************************************************************/
//void sd_info_cmd(sl_cli_command_arg_t *arguments)
//{
//  printf("Still need to implement!\r\n");
//
//  /* Info to get:
//   * - Card mfr info (mfr name, etc)
//   *  * For this we will need to read the CID and compare against a list of mfrs
//   * - Filesystem format (FAT32/EXFAT)
//   * - Filesystem size
//   * - Filesystem free space amt
//   * - Number of files on filesystem
//   */
//
//  char fs_type[6];
//  uint32_t fs_size;
//  uint32_t fs_free;
//  uint32_t fs_used;
//  float fs_uspc;
//  float fs_uspc_dec;
//
//
//  FATFS* fs_ptr = mod_sd_get_fs();
//
//
//  // Determine filesystem type
//  switch(fs_ptr->fs_type){
//    case FS_FAT12:
//      strcpy(fs_type, "FAT12");
//      break;
//    case FS_FAT16:
//      strcpy(fs_type, "FAT16");
//      break;
//    case FS_FAT32:
//      strcpy(fs_type, "FAT32");
//      break;
//    case FS_EXFAT:
//      strcpy(fs_type, "EXFAT");
//      break;
//    default:
//      strcpy(fs_type, "ERROR");
//      break;
//  }
//
//
//  // Determine size info
//  // ">> 1" is equivalent to "multiply by 512 to get bytes, then divide by 1024 to get kilobytes"
//  fs_size = (fs_ptr->n_fatent  * fs_ptr->csize) >> 1;
//  fs_free = (fs_ptr->free_clst * fs_ptr->csize) >> 1;
//  fs_used = fs_size - fs_free;
//  fs_uspc = (float) (fs_used * 100) / fs_size;
//  fs_uspc_dec = (float)(fs_uspc - ((uint8_t)fs_uspc))*100;
//
//  printf("fs_type: %s\r\n", fs_type);
//  printf("fs_size: %lu kB\r\n", fs_size);
//  printf("fs_free: %lu kB\r\n", fs_free);
//  printf("fs_used: %lu kB\r\n", fs_used);
//  printf("test%%: %lu\r\n", (uint32_t)fs_uspc);
//  printf("used%%: %u.%u%%\r\n", (uint8_t)fs_uspc, (uint8_t)fs_uspc_dec);
//
//
//  mod_sd_bytecount_t bc_size;
//  mod_sd_bytecount_t bc_free;
//  mod_sd_bytecount_t bc_used;
//
//  mod_sd_get_bytecount(fs_size, &bc_size);
//  mod_sd_get_bytecount(fs_free, &bc_free);
//  mod_sd_get_bytecount(fs_used, &bc_used);
//
//  printf("\r\n");
//  printf("bc_size: %u.%u %cB\r\n", bc_size.val, bc_size.dec, bc_size.pfx);
//  printf("bc_free: %u.%u %cB\r\n", bc_free.val, bc_free.dec, bc_free.pfx);
//  printf("bc_used: %u.%u %cB\r\n", bc_used.val, bc_used.dec, bc_used.pfx);
//}

/****************************************************************************//**
 * Callback for sd_unmount_cmd
 *
 * The command is used to unmount and close the SD card.
 ******************************************************************************/
void sd_close_and_unmount_cmd(sl_cli_command_arg_t *arguments)
{
  (void)arguments; // must accept as param but no need to use
  mod_sd_close_and_unmount_AW();
}

/****************************************************************************//**
 * Callback for sd_set_time_cmd
 *
 * The command is used to set the time.
 ******************************************************************************/
void sd_set_time_cmd(sl_cli_command_arg_t *arguments){
  if (sl_cli_get_argument_count(arguments)<6){
      printf("usage: set_time YYYY MM DD HH MM SS\r\n");
      printf("ex:    set_time 2000 12 25 12 30 01\r\n");
      return;
  }
  uint16_t year = sl_cli_get_argument_uint16(arguments, 0);
  uint8_t  month = sl_cli_get_argument_uint8(arguments, 1);
  uint8_t  day   = sl_cli_get_argument_uint8(arguments, 2);
  uint8_t  hour  = sl_cli_get_argument_uint8(arguments, 3);
  uint8_t  min   = sl_cli_get_argument_uint8(arguments, 4);
  uint8_t  sec   = sl_cli_get_argument_uint8(arguments, 5);

  sl_sleeptimer_date_t date;
  sl_sleeptimer_build_datetime(&date,year,(sl_sleeptimer_month_t)(month-1),day,hour,min,sec,0);
  sl_sleeptimer_set_datetime(&date);
  printf("Time set: %04u-%02u-%02u %02u:%02u:%02u\r\n", year, month, day, hour, min, sec);
}

/****************************************************************************//**
 * Callback for sd_sget_time_cmd
 *
 * The command is used to get the time of sd card.
 ******************************************************************************/
void get_time_cmd(sl_cli_command_arg_t *arguments){
  (void)arguments;
  sl_sleeptimer_date_t date;
  sl_sleeptimer_get_datetime(&date);
  printf("Current time: %04u-%02u-%02u %02u:%02u:%02u\r\n",
         date.year + 1900,
         date.month + 1,
         date.month_day,
         date.hour,
         date.min,
         date.sec);
}


/*******************************************************************************
 **************************   GLOBAL FUNCTIONS   *******************************
 ******************************************************************************/

/*******************************************************************************
 * Initialize cli example.
 ******************************************************************************/
void cli_app_init(void)
{
  bool status;

  status = sl_cli_command_add_command_group(sl_cli_inst_handle, command_group);
  EFM_ASSERT(status);

//  printf("\r\nStarted CLI Micrium OS Example\r\n\r\n");
  printf("---------------------------------\r\n");
  printf("---------------------------------\r\n");
  printf("Started CLI Micrium OS\r\n");

  printf("Useful CLI Options:\r\n");
  printf("- set_time to set the time of the sd card\r\n");
  printf("- get_time to get the current system time\r\n");
  printf("- sd_close_unmount to close and unmount the sd card to prevent corruption\r\n\r\n");

  printf("Please wait for the following initialization messages: successful Fat FS mount, file creation, and sensor found\r\n\r\n");

  printf("Recommend: use set_time to update SD card time:\r\n");
  printf("---------------------------------\r\n");
}
