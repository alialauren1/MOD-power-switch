/***************************************************************************//**
 * @file sdio.c
 * @brief SDIO handler library - initial
 * @version 0.0.1
 *******************************************************************************
 * # License
 * <b>Copyright 2018 Silicon Laboratories, Inc. www.silabs.com</b>
 *******************************************************************************
 *
 * Permission is granted to anyone to use this software for any purpose,
 * including commercial applications, and to alter it and redistribute it
 * freely, subject to the following restrictions:
 *
 * 1. The origin of this software must not be misrepresented; you must not
 *    claim that you wrote the original software.@n
 * 2. Altered source versions must be plainly marked as such, and must not be
 *    misrepresented as being the original software.@n
 * 3. This notice may not be removed or altered from any source distribution.
 *
 * DISCLAIMER OF WARRANTY/LIMITATION OF REMEDIES: Silicon Labs has no
 * obligation to support this Software. Silicon Labs is providing the
 * Software "AS IS", with no express or implied warranties of any kind,
 * including, but not limited to, any implied warranties of merchantability
 * or fitness for any particular purpose or warranties against infringement
 * of any proprietary rights of a third party.
 *
 * Silicon Labs will not be liable for any consequential, incidental, or
 * special damages, or any other relief, or for any claim by any third party,
 * arising from your use of this Software.
 *
 ******************************************************************************/
#include "sdio.h"
#include "em_device.h"
#include "em_cmu.h"
//#include "em_gpio.h"

#include "diskio.h"
#include "sl_sleeptimer.h"

//#include "mod_sd.h"
#include "mod_som_config.h"
//#include "FreeRTOS.h"
//#include "task.h"
#include "os.h"

/******************************************************************************
 * Local defines
 *****************************************************************************/
#define LOCAL_STA_NODISK    0x02
#define WAIT_TIME_OUT_MS    1000UL
/******************************************************************************
 * Local prototypes
 *****************************************************************************/
static SD_RES SDIO_S_SetClockFreq(SDIO_TypeDef *sdio_t,
                                  uint32_t sdio_freq,
                                  CMU_Clock_TypeDef main_clock);
static SD_RespType_t SDIO_S_GetRespType(uint32_t cmd_u32);
static uint32_t SDIO_S_GetResponseAndCheckBits(uint32_t cmd_u32, uint32_t arg_u32);
static uint32_t SDIO_S_SetDataPresentSelect(uint32_t cmd_u32);
static uint32_t SDIO_S_SendCMDWithOutDAT( SDIO_TypeDef *sdio_t,
                                        uint32_t cmd_u32,
                                        uint32_t cmdArg_u32);
static void SDIO_S_LowLevelRegisterInit(SDIO_TypeDef *sdio_t,
                                        uint32_t sdioFreq_u32,
                                        CMU_Clock_TypeDef mainClock_t);
static uint8_t SDIO_S_CardInitialization_and_Identification(SDIO_TypeDef *sdio_t);
static void SDIO_S_CardStdbyToTrsfrMode(SDIO_TypeDef *sdio_t);
static void SDIO_S_TimeoutSettingonDATLine(SDIO_TypeDef *sdio_t);

static void SDIO_S_SetUpADMA2(SDIO_TypeDef *sdio_t,
                              SD_ADMA2_Desc_t *adma2_desc,
                              uint32_t *sys_addr,
                              uint32_t blocks);
static uint32_t SDIO_S_TFRMODE(uint32_t cmd_u32, uint32_t arg_u32);
static SD_RES SDIO_S_ErrorCheck(uint32_t ifcr);
static uint8_t SDIO_S_WaitForFlag(uint32_t ien_mask, SD_WaitType_t use_waitloop);
static void SDIO_S_ClearFlag(uint32_t flag_mask);


void SDIO_GetR2(SDIO_TypeDef *sdio_t, uint32_t *buf);


/******************************************************************************
 * Local variables
 *****************************************************************************/
static struct
{
  uint32_t cardType_u3  :3;		// Card Type
  uint32_t cardRCA_u16  :16;	    // Card Relative Address
  uint32_t F8_u1        :1; 			// CMD8 valid flag
  uint32_t SDIO_u1      :1; 		    // SDIO type flag
  uint32_t CCS_u1       :1; 			// CARD Capacity Status bit's flag
  uint32_t S18A_u1      :1;			// 1.8V Support flag
} SDIO_ActCardState_st;


static uint32_t int_flags;

#define SD_STATUS_BUF_LEN 16
static uint32_t sd_status_buf[SD_STATUS_BUF_LEN];

#define CSD_BUF_LEN 4
static uint32_t csd_buf[CSD_BUF_LEN];

static SD_ADMA2_Desc_t adma2_desc;

//static TaskHandle_t calling_task_handle;
static OS_TCB* calling_task_tcb;
static RTOS_ERR err;

/**************************************************************************//**
 * @brief General initialization function for the SDIO host and for
 * 			SD cards
 *
 * @Inputs:
 * 		 SDIO_TypeDef *sdio_t: address of the MCU's SDIO register related
 * 		 					   memory mapped register
 * 		 uint32_t sdioFreq_u32: desired SDIO clock frequency (between host and
 * 		                        sd card)
 * 		 CMU_Clock_TypeDef mainClock_t: used main clock source
 *****************************************************************************/
uint8_t SDIO_Init(SDIO_TypeDef *sdio_t,
               uint32_t sdioFreq_u32,
               CMU_Clock_TypeDef mainClock_t)
{
  CMU_ClockEnable(cmuClock_SDIO, true);
  SDIO_S_LowLevelRegisterInit(sdio_t, sdioFreq_u32, mainClock_t);
  SDIO_S_SetClockFreq(sdio_t, MOD_SOM_FATFS_INIT_SDCLK_FREQ, mainClock_t);
  uint8_t status = SDIO_S_CardInitialization_and_Identification(sdio_t);
  if(status) return status;
  SDIO_S_CardStdbyToTrsfrMode(sdio_t);

  SDIO_S_SetClockFreq(sdio_t, sdioFreq_u32, mainClock_t);

  return status;
}


// 2025 12 16 LW: Function for cleanly setting SDIO clock frequency
SD_RES SDIO_S_SetClockFreq(SDIO_TypeDef *sdio_t,
                           uint32_t sdio_freq,
                           CMU_Clock_TypeDef main_clock)
{
  SD_RES ret = SD_OK;

  // Calculate divisor for new SDCLK frequency
  uint32_t divisor_u32 = (CMU_ClockFreqGet(main_clock) / sdio_freq) / 2;

  // Break out if the given frequency is out of spec
  if( (divisor_u32 >> 10) > 0 )
  {
    return SD_ERR;
  }

  // Save previous CLOCKCTRL settings
  uint32_t prev_clockctrl = sdio_t->CLOCKCTRL;

  // Disable SDCLK while we change the frequency
  sdio_t->CLOCKCTRL &= ~(SDIO_CLOCKCTRL_INTCLKEN
                        | SDIO_CLOCKCTRL_SDCLKEN);

  // Clear previous SDCLK frequency setting
  sdio_t->CLOCKCTRL &= ~(_SDIO_CLOCKCTRL_SDCLKFREQSEL_MASK
                        | _SDIO_CLOCKCTRL_UPPSDCLKFRE_MASK);

  // Set new frequency division and enable internal clock
  uint8_t divupp_u8 = (divisor_u32 & 0x300) >> 8;
  sdio_t->CLOCKCTRL |=  ((divisor_u32 << _SDIO_CLOCKCTRL_SDCLKFREQSEL_SHIFT) & _SDIO_CLOCKCTRL_SDCLKFREQSEL_MASK)
                      | (divupp_u8 << _SDIO_CLOCKCTRL_UPPSDCLKFRE_SHIFT)
                      | (SDIO_CLOCKCTRL_INTCLKEN);

  // Wait for clock to stabilize
  // TODO: Add a timeout here?
  while( !(sdio_t->CLOCKCTRL & SDIO_CLOCKCTRL_INTCLKSTABLE) );

  // Enable clock output
  // TODO: Only enable output if it was enabled previously?
  sdio_t->CLOCKCTRL |= SDIO_CLOCKCTRL_SDCLKEN;

  return ret;
}

/**************************************************************************//**
 * @brief This function copies 512byte data from SD card to MCU
 *
 * @Inputs:
 * 		 SDIO_TypeDef *sdio_t: address of the MCU's SDIO register related
 * 		 					   memory mapped register
 *
 * 		 SD_origin_u32: address of the SD card block, from where the data will
 * 		 				be copied
 *
 * 		 *destination_pu32: address of the data space, where the data will be
 * 		 					transfered from SD card. It shall be 512byte
 *****************************************************************************/
SD_RES SDIO_ReadSingleBlock(SDIO_TypeDef *sdio_t,
                          uint32_t SD_origin_u32,
                          uint32_t* destination_pu32)
{
  SDIO_S_TimeoutSettingonDATLine(sdio_t);

  SEGGER_SYSVIEW_ErrorfHost("RSB %d", SD_origin_u32);

  SDIO_S_ClearFlag( (_SDIO_IFCR_CMDCOM_MASK | _SDIO_IFCR_BFRRDRDY_MASK | _SDIO_IFCR_TRANCOM_MASK) );

  SDIO_S_SetUpADMA2(sdio_t, &adma2_desc, destination_pu32, 1);

  SD_RES status = SDIO_IssueCMD(sdio_t, CMD17, SD_origin_u32, 1);

  // 19. Wait for transfer completed int
  SDIO_S_WaitForFlag( (_SDIO_IFCR_TRANCOM_MASK | _SDIO_IFCR_DATTOUTERR_MASK | _SDIO_IFCR_ADMAERR_MASK ) , SD_INTERRUPT_MODE);

  status = SDIO_S_ErrorCheck(sdio_t->IFCR);

  // 20. CLear Transfer completed status
  SDIO_S_ClearFlag(_SDIO_IFCR_TRANCOM_MASK);

  return status;
}

SD_RES SDIO_ReadMultipleBlocks(SDIO_TypeDef *sdio_t,
                                uint32_t SD_origin_u32,
                                uint32_t* destination_pu32,
                                uint32_t blocks)
{
  SDIO_S_TimeoutSettingonDATLine(sdio_t);

  SEGGER_SYSVIEW_ErrorfHost("RMB %d %d", SD_origin_u32, blocks);

  SDIO_S_SetUpADMA2(sdio_t, &adma2_desc, destination_pu32, blocks);

  SD_RES status = SDIO_IssueCMD(sdio_t, CMD18, SD_origin_u32, blocks);

  // 19. Wait for transfer completed int
  SDIO_S_WaitForFlag( (_SDIO_IFCR_TRANCOM_MASK | _SDIO_IFCR_DATTOUTERR_MASK | _SDIO_IFCR_ADMAERR_MASK ) , SD_INTERRUPT_MODE);

  status = SDIO_S_ErrorCheck(sdio_t->IFCR);

  // 20. CLear Transfer completed status
  SDIO_S_ClearFlag(_SDIO_IFCR_TRANCOM_MASK);

  return status;
}

/**************************************************************************//**
 * @brief This function copies 512byte data from MCU to the memory card
 *
 * @Inputs:
 * 		 SDIO_TypeDef *sdio_t: address of the MCU's SDIO register related
 * 		 					   memory mapped register
 *
 * 		 SD_dest_u32: address of the SD card block, where the data will be
 * 		 			  copied
 *
 * 		 *origin_pu32: address of the data, from the data will be transfered
 * 		 				to SD card. It shall be 512byte
 *****************************************************************************/
SD_RES SDIO_WriteSingleBlock( SDIO_TypeDef *sdio_t,
                            uint32_t SD_dest_u32,
                            uint32_t* origin_pu32)
{

  SDIO_S_TimeoutSettingonDATLine(sdio_t);

  SEGGER_SYSVIEW_ErrorfHost("WSB %d", SD_dest_u32);

  SDIO_S_ClearFlag( (_SDIO_IFCR_CMDCOM_MASK | _SDIO_IFCR_BFRWRRDY_MASK | _SDIO_IFCR_TRANCOM_MASK) );

  SDIO_S_SetUpADMA2(sdio_t, &adma2_desc, origin_pu32, 1);

  SD_RES status = SDIO_IssueCMD(sdio_t, CMD24, SD_dest_u32, 1);

  // 19. Wait for transfer completed int
  SDIO_S_WaitForFlag( (_SDIO_IFCR_TRANCOM_MASK | _SDIO_IFCR_DATTOUTERR_MASK | _SDIO_IFCR_ADMAERR_MASK ) , SD_INTERRUPT_MODE);

  status = SDIO_S_ErrorCheck(sdio_t->IFCR);

  // 20. CLear Transfer completed status
  SDIO_S_ClearFlag(_SDIO_IFCR_TRANCOM_MASK);

  SEGGER_SYSVIEW_PrintfHost("DONE");

  return status;
}



SD_RES SDIO_WriteMultipleBlocks(SDIO_TypeDef *sdio_t,
                           uint32_t SD_dest_u32,
                           uint32_t* origin_pu32,
                           uint32_t blocks)
{
//  GPIO_PinModeSet(gpioPortA, 6, gpioModePushPull, 1);


  SDIO_S_TimeoutSettingonDATLine(sdio_t);

  SD_RES status = SD_OK;

  SEGGER_SYSVIEW_ErrorfHost("WMB %d %d", SD_dest_u32, blocks);


  SDIO_S_SetUpADMA2(sdio_t, &adma2_desc, origin_pu32, blocks);

  status = SDIO_IssueCMD(sdio_t, CMD25, SD_dest_u32, blocks);

  // 19. Wait for transfer completed int
  SDIO_S_WaitForFlag( (_SDIO_IFCR_TRANCOM_MASK | _SDIO_IFCR_DATTOUTERR_MASK | _SDIO_IFCR_ADMAERR_MASK ) , SD_INTERRUPT_MODE);

  status = SDIO_S_ErrorCheck(sdio_t->IFCR);

  // 20. CLear Transfer completed status
  SDIO_S_ClearFlag(_SDIO_IFCR_TRANCOM_MASK);

  SEGGER_SYSVIEW_PrintfHost("DONE");
//  GPIO_PinModeSet(gpioPortA, 6, gpioModePushPull, 0);
  return status;
}


/**************************************************************************//**
 * @brief Low level procedure for the commands, which do not use data transfer
 * 		  between host and SD card under execution
 *
 * @Inputs:
 * 		 SDIO_TypeDef *sdio_t: address of the MCU's SDIO register related
 * 		 					   memory mapped register
 *
 * 		 cmd_u32: the used command
 *
 * 		 cmdArg_u32: the command related argument
 *****************************************************************************/
uint32_t SDIO_S_SendCMDWithOutDAT(SDIO_TypeDef *sdio_t,
                                uint32_t cmd_u32,
                                uint32_t cmdArg_u32)
{
  return (uint32_t)SDIO_IssueCMD(sdio_t, cmd_u32, cmdArg_u32, 0);
}



SD_RES SDIO_IssueCMD(SDIO_TypeDef *sdio_t,
                      uint32_t cmd_u32,
                      uint32_t cmdArg_u32,
                      uint32_t blocks)
{
  SD_RES status = SD_OK;
  uint32_t tmpReg_u32;
  uint32_t regVal_u32 = sdio_t->PRSSTAT;


  // Clear flags
  sdio_t->IFCR = 0;

  // Set number of data blocks to transfer
  sdio_t->BLKSIZE = ( (0x200 << _SDIO_BLKSIZE_TFRBLKSIZE_SHIFT) | (blocks << _SDIO_BLKSIZE_BLKSCNTFORCURRTFR_SHIFT) );

  //1. Check Command Inhibit Used
  while (regVal_u32 & _SDIO_PRSSTAT_CMDINHIBITCMD_MASK)
  {
    regVal_u32 = sdio_t->PRSSTAT;
  }

  // 2. Check command with busy
  if (cmd_u32 == CMD7 ||
      cmd_u32 == CMD12)
  {
    // with busy
    // Check Abort Commands
    if (cmd_u32 == CMD12)
    {
      // abort command?
    }
    else
    {
      while (sdio_t->PRSSTAT & _SDIO_PRSSTAT_CMDINHIBITDAT_MASK);
    }
  }

  /* 5. Set Argument 1 Reg */
  sdio_t->CMDARG1 = cmdArg_u32;

  /* 6. Set Command Reg */
  tmpReg_u32 = SDIO_S_TFRMODE(cmd_u32, cmdArg_u32);

  // Issue Command
  sdio_t->TFRMODE = tmpReg_u32;


  // 2025 12 05 LW: Suspend the task and resume once we get an interrupt for command completion or error
  // TODO: Check this
//  SD_WaitType_t wait_type = SD_SPINLOOP_MODE;
//  if(cmd_u32 == ACMD41) wait_type = SD_INTERRUPT_MODE;
  SDIO_S_WaitForFlag( (_SDIO_IFCR_CMDCOM_MASK | _SDIO_IFCR_CMDTOUTERR_MASK) , SD_SPINLOOP_MODE);

  status = SDIO_S_ErrorCheck(sdio_t->IFCR);

  // Capture whether or not the command had an error
  if(status != SD_OK)
  {
      sdio_t->CLOCKCTRL |= (_SDIO_CLOCKCTRL_SFTRSTCMD_MASK);
  }

  // 2. clear previous command complete int
  SDIO_S_ClearFlag(_SDIO_IFCR_CMDCOM_MASK);

  // 2025 12 16 LW: Debugging f_mkfs()
  if(cmd_u32 == CMD7)
  {
    SDIO_S_ClearFlag(_SDIO_IFCR_TRANCOM_MASK);
  }

  return status;
}


uint32_t SDIO_S_TFRMODE(uint32_t cmd_u32, uint32_t arg_u32)
{

  // Start with default TFRMODE
  uint32_t tfrmode = DEFLT_TFRMODE;

  // Set response type and index/CRC check based on the CMD given
  tfrmode |= SDIO_S_GetResponseAndCheckBits(cmd_u32, arg_u32);

  // Set CMD-specific options
  switch(cmd_u32)
  {
    case CMD12:
      tfrmode |= CMD12_TFRMODE;
      break;
    case CMD17:
      tfrmode |= CMD17_TFRMODE;
      break;
    case CMD18:
      tfrmode |= CMD18_TFRMODE;
      break;
    case CMD24:
      tfrmode |= CMD24_TFRMODE;
      break;
    case CMD25:
      tfrmode |= CMD25_TFRMODE;
      break;
    case ACMD13:
      tfrmode |= ACMD13_TFRMODE;
      break;
  }

  // Set the CMD index
  tfrmode |= (cmd_u32 << _SDIO_TFRMODE_CMDINDEX_SHIFT);

  return tfrmode;
}

SD_RES SDIO_S_ErrorCheck(uint32_t ifcr)
{
  SD_RES res = SD_OK;

  if(ifcr & _SDIO_IFCR_ERRINT_MASK) res = SD_ERR;

  if(ifcr & _SDIO_IFCR_CMDTOUTERR_MASK)        res = SD_CMDTOUTERR;
  else if(ifcr & _SDIO_IFCR_CMDCRCERR_MASK)    res = SD_CMDCRCERR;
  else if(ifcr & _SDIO_IFCR_CMDENDBITERR_MASK) res = SD_CMDENDBITERR;
  else if(ifcr & _SDIO_IFCR_CMDINDEXERR_MASK)  res = SD_CMDINDEXERR;
  else if(ifcr & _SDIO_IFCR_DATTOUTERR_MASK)   res = SD_DATTOUTERR;
  else if(ifcr & _SDIO_IFCR_DATCRCERR_MASK)    res = SD_DATCRCERR;
  else if(ifcr & _SDIO_IFCR_DATENDBITERR_MASK) res = SD_DATENDBITERR;
  else if(ifcr & _SDIO_IFCR_ADMAERR_MASK)      res = SD_ADMAERR;

  if(res != SD_OK)
  {
    printf("SD Error: %d\r\n", (uint8_t)res);
  }

  return res;
}


static void SDIO_S_SetUpADMA2(SDIO_TypeDef *sdio_t,
                              SD_ADMA2_Desc_t *adma2_desc,
                              uint32_t *sys_addr,
                              uint32_t blocks)
{
  adma2_desc->addr = (uint32_t)sys_addr;
  adma2_desc->len = ((uint16_t)blocks) * 512;
  // TODO: Add support for generating linked descriptors for transfers of more than 128 blocks
  adma2_desc->ctl = (ADMA2_ACT2 | ADMA2_END | ADMA2_VLID);

  sdio_t->ADSADDR = (uint32_t)adma2_desc;
}


/**************************************************************************//**
 * @brief Set the data transition related timeout (max time)
 *
 * @Inputs:
 * 		 SDIO_TypeDef *sdio_t: address of the MCU's SDIO register related
 * 		 					   memory mapped register
 *
 *****************************************************************************/
static void SDIO_S_TimeoutSettingonDATLine(SDIO_TypeDef *sdio_t)
{

  // 1. Calculate divisor to determine timeout errors

  // 2. Set Data Timeout Counter Value
  sdio_t->CLOCKCTRL |= (0xFE << _SDIO_CLOCKCTRL_DATTOUTCNTVAL_SHIFT);
}

// 2025 12 16 LW: Implemented switch statement for determining command's response type
/**************************************************************************//**
 * @brief Helper function for SDIO_S_GetResponseAndCheckBits(). Based on the
 * 		  command name, the return value will be the command related response.
 *
 * @Inputs:
 * 		 uint32_t cmd_u32: SD memory card command
 *
 * @Outputs:
 * 		 Function's return value: the desired command related response type
 *****************************************************************************/
static SD_RespType_t SDIO_S_GetRespType(uint32_t cmd_u32)
{
  SD_RespType_t resp_type;

  // Determine response type for given command (default to R1)
  switch(cmd_u32)
  {
    // No response.
    case CMD0:
      resp_type = SD_RESP_NONE;
      break;

    // R1b: Normal w/ busy.
    case CMD7:
    case CMD12:
      resp_type = SD_RESP_R1b;
      break;

    // R2: CID/CSD register.
    case CMD2:
    case CMD9:
    case CMD10:
      resp_type = SD_RESP_R2;
      break;

    // R3: OCR register.
    case ACMD41:
      resp_type = SD_RESP_R3;
      break;

    // R6: Published RCA.
    case CMD3:
    case CMD5:
      resp_type = SD_RESP_R6;
      break;

    // R7: Interface cond.
    case CMD8:
      resp_type = SD_RESP_R7;
      break;

    // R1: Normal response.
    default:
      resp_type = SD_RESP_R1;
      break;
  }

  return resp_type;
}

// 2025 12 16 LW: Implemented switch statement for setting TFRMODE resp & index/crc check bits
/**************************************************************************//**
 * @brief Helper function for SDIO_S_TFRMODE(). Based on the command name,
 *     the return value will be the TFRMODE register bits for the
 *     response type and CMD Index / CRC check for that response type.
 *
 * @Inputs:
 *     uint32_t cmd_u32: SD memory card command
 *
 * @Outputs:
 *     Function's return value: TFRMODE bits for resp/indexchk/crcchk
 *****************************************************************************/
static uint32_t SDIO_S_GetResponseAndCheckBits(uint32_t cmd_u32, uint32_t arg_u32)
{
  uint32_t tfrmode = 0;

  switch(SDIO_S_GetRespType(cmd_u32))
  {
    // No response.
    case SD_RESP_NONE:
      tfrmode = SDIO_TFRMODE_RESPTYPESEL_NORESP;
      break;

    // R1: Normal response.
    case SD_RESP_R1:
      tfrmode = (SDIO_TFRMODE_RESPTYPESEL_RESP48
                | SDIO_TFRMODE_CMDCRCCHKEN_ENABLE
                | SDIO_TFRMODE_CMDINDXCHKEN_ENABLE);
      break;

    // R1b: Normal w/ busy.
    case SD_RESP_R1b:
      if( (cmd_u32 == CMD7) && (arg_u32 != 0) )
      {
        tfrmode = SDIO_TFRMODE_RESPTYPESEL_BUSYAFTRESP;
      }
      else
      {
        tfrmode = SDIO_TFRMODE_RESPTYPESEL_NORESP;
      }
      tfrmode |= (SDIO_TFRMODE_CMDCRCCHKEN_ENABLE
                 | SDIO_TFRMODE_CMDINDXCHKEN_ENABLE);

      break;

    // R2: CID/CSD register.
    case SD_RESP_R2:
      tfrmode = (SDIO_TFRMODE_RESPTYPESEL_RESP136
                | SDIO_TFRMODE_CMDCRCCHKEN_ENABLE);
      break;

    // R3: OCR register.
    case SD_RESP_R3:
      tfrmode = SDIO_TFRMODE_RESPTYPESEL_RESP48;
      break;

    // R6: Published RCA.
    case SD_RESP_R6:
      tfrmode = (SDIO_TFRMODE_RESPTYPESEL_RESP48
                | SDIO_TFRMODE_CMDCRCCHKEN_ENABLE
                | SDIO_TFRMODE_CMDINDXCHKEN_ENABLE);
      break;

    // R7: Interface cond.
    case SD_RESP_R7:
      tfrmode = (SDIO_TFRMODE_RESPTYPESEL_RESP48
                | SDIO_TFRMODE_CMDCRCCHKEN_ENABLE
                | SDIO_TFRMODE_CMDINDXCHKEN_ENABLE);
      break;
  }

  return tfrmode;
}

/**************************************************************************//**
 * @brief Low level SDIO host initialization
 * 		  For more information see:
 * 		  SD_Host_Controller_Simplified_Specification_Ver3.00 Chapter2.x
 *
 * @Inputs:
 * 		 SDIO_TypeDef *sdio_t: address of the MCU's SDIO register related
 * 		 					   memory mapped register
 * 		 uint32_t sdioFreq_u32: desired SDIO clock frequency (between host and
 *                              sd card)
 *       CMU_Clock_TypeDef mainClock_t: used main clock source
 *****************************************************************************/
static void SDIO_S_LowLevelRegisterInit(SDIO_TypeDef *sdio_t,
                                        uint32_t sdioFreq_u32,
                                        CMU_Clock_TypeDef mainClock_t)
{
  /*
   * Board specific register adjustment
   * Route soldered microSD card slot
   */
  //ALB Make CD LOC since the SOM use LOC0 (F8) but for some reason it chokes when I do this
  sdio_t->ROUTELOC0 = 	SDIO_ROUTELOC0_DATLOC_LOC1 
						| SDIO_ROUTELOC0_CDLOC_LOC3
						//| SDIO_ROUTELOC0_WPLOC_LOC3
						| SDIO_ROUTELOC0_CLKLOC_LOC1;
  sdio_t->ROUTELOC1 = 	SDIO_ROUTELOC1_CMDLOC_LOC1;

  sdio_t->ROUTEPEN = 	SDIO_ROUTEPEN_CLKPEN 
						| SDIO_ROUTEPEN_CMDPEN
						| SDIO_ROUTEPEN_D0PEN 
						| SDIO_ROUTEPEN_D1PEN 
						| SDIO_ROUTEPEN_D2PEN
						| SDIO_ROUTEPEN_D3PEN;

  /*
   * General SDIO register adjustment
   */
  sdio_t->CTRL = 		(0 << _SDIO_CTRL_ITAPDLYEN_SHIFT)
						| (0 << _SDIO_CTRL_ITAPDLYSEL_SHIFT) 
						| (0 << _SDIO_CTRL_ITAPCHGWIN_SHIFT)
						| (1 << _SDIO_CTRL_OTAPDLYEN_SHIFT) 
						| (8 << _SDIO_CTRL_OTAPDLYSEL_SHIFT);

  sdio_t->CFG0 =        (0x20 << _SDIO_CFG0_TUNINGCNT_SHIFT)
                        | (0x30 << _SDIO_CFG0_TOUTCLKFREQ_SHIFT)
                        | (1 << _SDIO_CFG0_TOUTCLKUNIT_SHIFT)
                        | (0xD0 << _SDIO_CFG0_BASECLKFREQ_SHIFT)
                        | (SDIO_CFG0_MAXBLKLEN_1024B)
                        | (1 << _SDIO_CFG0_C8BITSUP_SHIFT)
                        | (1 << _SDIO_CFG0_CADMA2SUP_SHIFT)
                        | (1 << _SDIO_CFG0_CHSSUP_SHIFT)
                        | (1 << _SDIO_CFG0_CSDMASUP_SHIFT)
                        | (1 << _SDIO_CFG0_CSUSPRESSUP_SHIFT)
                        | (1 << _SDIO_CFG0_C3P3VSUP_SHIFT)
                        | (1 << _SDIO_CFG0_C3P0VSUP_SHIFT)
                        | (1 << _SDIO_CFG0_C1P8VSUP_SHIFT);

  sdio_t->CFG1 =        (0 << _SDIO_CFG1_ASYNCINTRSUP_SHIFT)
                        | (SDIO_CFG1_SLOTTYPE_EMSDSLOT)
                        | (1 << _SDIO_CFG1_CSDR50SUP_SHIFT)
                        | (1 << _SDIO_CFG1_CSDR104SUP_SHIFT)
                        | (1 << _SDIO_CFG1_CDDR50SUP_SHIFT)
                        | (1 << _SDIO_CFG1_CDRVASUP_SHIFT)
                        | (1 << _SDIO_CFG1_CDRVCSUP_SHIFT)
                        | (1 << _SDIO_CFG1_CDRVDSUP_SHIFT)
                        | (1 << _SDIO_CFG1_RETUNTMRCTL_SHIFT)
                        | (1 << _SDIO_CFG1_TUNSDR50_SHIFT)
                        | (0 << _SDIO_CFG1_RETUNMODES_SHIFT)
                        | (1 << _SDIO_CFG1_SPISUP_SHIFT)
                        | (1 << _SDIO_CFG1_ASYNCWKUPEN_SHIFT);

  sdio_t->CFGPRESETVAL0 = (0 << _SDIO_CFGPRESETVAL0_INITSDCLKFREQ_SHIFT)
                        | (0 << _SDIO_CFGPRESETVAL0_INITCLKGENEN_SHIFT)
                        | (0 << _SDIO_CFGPRESETVAL0_INITDRVST_SHIFT)
                        | (0x4 << _SDIO_CFGPRESETVAL0_DSPSDCLKFREQ_SHIFT)
                        | (0 << _SDIO_CFGPRESETVAL0_DSPCLKGENEN_SHIFT)
                        | (0x3 << _SDIO_CFGPRESETVAL0_DSPDRVST_SHIFT);

  sdio_t->CFGPRESETVAL1 = (2 << _SDIO_CFGPRESETVAL1_HSPSDCLKFREQ_SHIFT)
                        | (0 << _SDIO_CFGPRESETVAL1_HSPCLKGENEN_SHIFT)
                        | (2 << _SDIO_CFGPRESETVAL1_HSPDRVST_SHIFT)
                        | (4 << _SDIO_CFGPRESETVAL1_SDR12SDCLKFREQ_SHIFT)
                        | (0 << _SDIO_CFGPRESETVAL1_SDR12CLKGENEN_SHIFT)
                        | (1 << _SDIO_CFGPRESETVAL1_SDR12DRVST_SHIFT);

  sdio_t->CFGPRESETVAL2 = (2 << _SDIO_CFGPRESETVAL2_SDR25SDCLKFREQ_SHIFT)
                        | (0 << _SDIO_CFGPRESETVAL2_SDR25CLKGENEN_SHIFT)
                        | (0 << _SDIO_CFGPRESETVAL2_SDR25DRVST_SHIFT)
                        | (1 << _SDIO_CFGPRESETVAL2_SDR50SDCLKFREQ_SHIFT)
                        | (0 << _SDIO_CFGPRESETVAL2_SDR50CLKGENEN_SHIFT)
                        | (1 << _SDIO_CFGPRESETVAL2_SDR50DRVST_SHIFT);

  sdio_t->CFGPRESETVAL3 = (0 << _SDIO_CFGPRESETVAL3_SDR104SDCLKFREQ_SHIFT)
                        | (0 << _SDIO_CFGPRESETVAL3_SDR104CLKGENEN_SHIFT)
                        | (2 << _SDIO_CFGPRESETVAL3_SDR104DRVST_SHIFT)
                        | (2 << _SDIO_CFGPRESETVAL3_DDR50SDCLKFREQ_SHIFT)
                        | (0 << _SDIO_CFGPRESETVAL3_DDR50CLKGENEN_SHIFT)
                        | (3 << _SDIO_CFGPRESETVAL3_DDR50DRVST_SHIFT);

  sdio_t->IFENC =       SDIO_IFENC_CMDCOMEN
                        | SDIO_IFENC_TRANCOMEN
                        | SDIO_IFENC_BUFWRRDYEN
                        | SDIO_IFENC_BUFRDRDYEN
                        | SDIO_IFENC_CMDTOUTERREN
                        | SDIO_IFENC_CMDCRCERREN
                        | SDIO_IFENC_CMDENDBITERREN
                        | SDIO_IFENC_CMDINDEXERREN
                        | SDIO_IFENC_DATTOUTERREN
                        | SDIO_IFENC_DATCRCERREN
                        | SDIO_IFENC_CARDINSEN
                        | SDIO_IFENC_CARDRMEN
                        | SDIO_IFENC_CARDINTEN
                        | SDIO_IFENC_ADMAERREN
                        | SDIO_IFENC_DMAINTEN;

//  sdio_t->IEN =         SDIO_IEN_CMDCOMSEN
//                        | SDIO_IEN_TRANCOMSEN
//                        | SDIO_IEN_CMDTOUTERRSEN
//                        | SDIO_IEN_CMDCRCERRSEN
//                        | SDIO_IEN_CMDENDBITERRSEN
//                        | SDIO_IEN_CMDINDEXERRSEN
//                        | SDIO_IEN_DATTOUTERRSEN
//                        | SDIO_IEN_DATCRCERRSEN
//                        | SDIO_IEN_DATENDBITERRSEN;
//
//
//  NVIC_EnableIRQ(SDIO_IRQn);


  {

    // 2024 12 12 LW: Configure CMU for SDIOCLK to use HFXO
        CMU->SDIOCTRL = CMU_SDIOCTRL_SDIOCLKSEL_HFXO;

    // Calculate the divisor for SD clock frequency
    // 2024 12 12 LW: Divide divisor by 2 for correct frequency
//    uint32_t divisor_u32 = (CMU_ClockFreqGet(mainClock_t) / sdioFreq_u32) / 2;
//    sdio_t->CLOCKCTRL = ((divisor_u32 << _SDIO_CLOCKCTRL_SDCLKFREQSEL_SHIFT))
//                        | (SDIO_CLOCKCTRL_INTCLKEN)
//                        | (SDIO_CLOCKCTRL_SDCLKEN);
  }
//ALB I am changing to 3P3 becasue that is what we have
  sdio_t->HOSTCTRL1 =   (SDIO_HOSTCTRL1_SDBUSVOLTSEL_3P3V)
                        | (SDIO_HOSTCTRL1_SDBUSPOWER)
                        | (SDIO_HOSTCTRL1_DATTRANWD_SD4)
// 2025 12 17 LW: Added ADMA2 support
                        | (SDIO_HOSTCTRL1_DMASEL_ADMA2);
  //ALB
  //ALB CDSIGDET should trigger the toggling of the register CDTSTLLVL
//  SDIO->HOSTCTRL1|=(_SDIO_HOSTCTRL1_CDSIGDET_MASK & SDIO_HOSTCTRL1_CDSIGDET);
}

/**************************************************************************//**
 * @brief Initialization and identification sequence for the SDSC, SDHC and
 * 		  SDXC cards.
 * 		  This function covers the entry point of the card initialization
 * 		  (reset), then card identification mode, and at the end the card will
 * 		  enter to the data transfer mode.
 * 		  For the whole description see: SD Host Controller Simplified
 * 		  Specification Version 3.00 Chapter 3.6
 *
 * @Inputs:
 * 		 SDIO_TypeDef *sdio_t: address of the MCU's SDIO register related
 * 		 					   memory mapped register
 *
 * @Outputs:
 * 		 SDIO_ActCardState_st: the initialized SD card related informations
 *****************************************************************************/
static uint8_t SDIO_S_CardInitialization_and_Identification(SDIO_TypeDef *sdio_t)
{
  uint8_t status = 0;
  uint32_t tempVar_u32 = 0x0;
  uint8_t attemptCnt_u8 = 2;
  uint32_t resp = 0;
//  //ALB add result of SDIO_S_SendCMDWithOutDAT
//  uint8_t return_SendCMDWithOutDAT=0;

  // 1. Reset Card
//  return_SendCMDWithOutDAT=SDIO_S_SendCMDWithOutDAT(sdio_t, CMD0, 0);
  SDIO_S_SendCMDWithOutDAT(sdio_t, CMD0, 0);
//  if (return_SendCMDWithOutDAT==LOCAL_STA_NODISK){
//      disk_status (return_SendCMDWithOutDAT);
//  }
  while (attemptCnt_u8 != 0)
  {
    // 2. Voltage Check
    tempVar_u32 = sdio_t->RESP0;

    resp = SDIO_S_SendCMDWithOutDAT(sdio_t, CMD8, 1 << _CMD8_27V_36V_SHIFT);

    // 3. Check response
    if (sdio_t->RESP0 == tempVar_u32 && resp == 0)
    {
      // no response
      SDIO_ActCardState_st.F8_u1 = 0;
      attemptCnt_u8 = 0;
    } else
    {
      // valid response
      if ((!(int_flags & SDIO_IFCR_CMDCRCERR)) &&
          (!(int_flags & SDIO_IFCR_CMDINDEXERR)) &&
          ((sdio_t->RESP0 & 1 << _CMD8_27V_36V_SHIFT) == 1 << _CMD8_27V_36V_SHIFT))
      {
       // Good response
        SDIO_ActCardState_st.F8_u1 = 1;
        attemptCnt_u8 = 0;
      } else
      {
        attemptCnt_u8--;
        if (attemptCnt_u8 == 1)
        {
          // 4. Unusable card
          return -1;
        }
      }
    }
  }

  // 5. Get SDIO OCR (CMD5) Voltage window = 0
  tempVar_u32 = sdio_t->RESP0;

  SDIO_ActCardState_st.SDIO_u1 = 0;

  // 11. Check F8 flag
  if (SDIO_ActCardState_st.F8_u1 == 1)
  {
    // 19.Get OCR(ACMD41) Voltage window = 0
    status = SDIO_S_SendCMDWithOutDAT(sdio_t, CMD55, RCA_DEFAULT << _CMD55_RCA_SHIFT);
    tempVar_u32 = sdio_t->RESP0;
    status = SDIO_S_SendCMDWithOutDAT(sdio_t, ACMD41, 0 << _ACMD41_VW_SHIFT); // voltage window = 0

    // 20. Check OCR
    if (tempVar_u32 != sdio_t->RESP0)
    {
      // OCR OK
      // 23. Check busy
      while (!(sdio_t->RESP0 & (1 << _ACMD41_RESPONSE_BUSY_BIT_SHIFT)))
      {
        // 2025 12 08 LW: Wait 100ms before sending ACMD41 again, allow other tasks to run in the meantime
//        vTaskDelay(configTICK_RATE_HZ / 10);
        volatile uint32_t tick_rate = OSTimeTickRateHzGet(NULL);
        OSTimeDly(((OS_TICK)tick_rate / 10), OS_OPT_TIME_DLY, &err);
        // 21. Initialization (ACMD41 HCS=1, S18R, XPC) Set Voltage Window
        SDIO_S_SendCMDWithOutDAT(sdio_t, CMD55,
        RCA_DEFAULT << _CMD55_RCA_SHIFT);
        SDIO_S_SendCMDWithOutDAT(sdio_t, ACMD41, 1 << _ACMD41_VW_31_32_SHIFT |
                                                 1 << _ACMD41_HCS_SHIFT |
                                                 1 << _ACMD41_XPC_SHIFT |
                                                 1 << _ACMD41_S18R_SHIFT);
        SDIO_ActCardState_st.CCS_u1 = ((sdio_t->RESP0 & 1 << 30) >> 30);
        SDIO_ActCardState_st.S18A_u1 = ((sdio_t->RESP0 & 1 << 24) >> 24);
      }

      // 24. Check CCS
      if (SDIO_ActCardState_st.CCS_u1 == 0)
      {
        // 25. Set SDSC Ver2.00 or Ver3.00
        SDIO_ActCardState_st.cardType_u3 = SDSC_Ver200_or_Ver300;
      } else
      {
        // 26. Set SDHC SDXC
        SDIO_ActCardState_st.cardType_u3 = SDHC_SDXC;
        // 27. Signal Voltage Switch Procedure
      }

    }
  } else
  {
    // F8=0
    // 12.Get OCR(ACMD41) Voltage window = 0
    SDIO_S_SendCMDWithOutDAT(sdio_t, CMD55, RCA_DEFAULT << _CMD55_RCA_SHIFT);
    tempVar_u32 = sdio_t->RESP0;
    status = SDIO_S_SendCMDWithOutDAT(sdio_t, ACMD41, 0 << _ACMD41_VW_SHIFT); // voltage window = 0

    // 2025 04 25 LW: Exit here if a card is inserted but not responding to commands
    if(status)
    {
        SDIO_ActCardState_st.cardType_u3 = Unusable_Card;
        return -1;
    }

    // 13. Check OCR
    if (tempVar_u32 != sdio_t->RESP0)
    {
      // OCR OK
      // 16. Check busy
      while (!(sdio_t->RESP0 & (1 << _ACMD41_RESPONSE_BUSY_BIT_SHIFT)))
      {
        // 21. Initialization (ACMD41 HCS=1, S18R, XPC) Set Voltage Window
        SDIO_S_SendCMDWithOutDAT(sdio_t, CMD55,
        RCA_DEFAULT << _CMD55_RCA_SHIFT);
        SDIO_S_SendCMDWithOutDAT(sdio_t, ACMD41, 1 << _ACMD41_VW_31_32_SHIFT);
        SDIO_ActCardState_st.CCS_u1 = ((sdio_t->RESP0 & 1 << 30) >> 30);
        SDIO_ActCardState_st.S18A_u1 = ((sdio_t->RESP0 & 1 << 24) >> 24);
        SDIO_ActCardState_st.cardType_u3 = SDSC_Ver101_or_Ver110;
      }
    } else
    {
      // OCR NOK
      // 17. Not SD Card
      SDIO_ActCardState_st.cardType_u3 = Not_SD_Card;
      return -1;
    }
  }

  // 32. Get CID (Card ID)
  SDIO_S_SendCMDWithOutDAT(sdio_t, CMD2, 0);

  // 33. Get RCA (relative card address)
  SDIO_S_SendCMDWithOutDAT(sdio_t, CMD3, 0);
  // The upper 16 bit of the response is the RCA
  SDIO_ActCardState_st.cardRCA_u16 = ((sdio_t->RESP0  & 0xFFFF0000) >> 16);

  return 0;
}

/**************************************************************************//**
 * @brief Perform state change (from card identification mode to data transfer
 *  		mode) in the SD card
 *
 * @Inputs:
 * 		 SDIO_TypeDef *sdio_t: address of the MCU's SDIO register related
 * 		 					   memory mapped register
 *
 *		 SDIO_ActCardState_st.cardRCA_u16: actual SD card related relative
 *		 								   address
 *
 *****************************************************************************/
static void SDIO_S_CardStdbyToTrsfrMode(SDIO_TypeDef *sdio_t)
{

  // 1. Send status task register - R1 response
  SDIO_S_SendCMDWithOutDAT(sdio_t, CMD13,
      SDIO_ActCardState_st.cardRCA_u16 << _CMD13_RCA_SHIFT);
  // 2. Select the actual card to active
  SDIO_S_SendCMDWithOutDAT(sdio_t, CMD7,
      SDIO_ActCardState_st.cardRCA_u16 << _CMD7_RCA_SHIFT);
  // 3. Change to 4bit mode
  SDIO_S_SendCMDWithOutDAT(sdio_t, CMD55,
      SDIO_ActCardState_st.cardRCA_u16 << _CMD55_RCA_SHIFT);
  SDIO_S_SendCMDWithOutDAT(sdio_t, ACMD6, ACMD6_4BIT_BUSWIDTH_SET);
}

/**************************************************************************//**
 * @brief Return with the type of the initialized card
 *
 * @Inputs:
 *       none
 *
 * @Outputs:
 *       return: the type of the card
 *****************************************************************************/
uint8_t SDIO_GetActCardStateType(void)
{
    return ((uint8_t)(SDIO_ActCardState_st.cardType_u3));
}


// 2025 04 28 LW: Return when there is no write in progress (for CTRL_SYNC)
void SDIO_WaitForWriteFinish(SDIO_TypeDef *sdio_t)
{
    while(sdio_t->PRSSTAT & _SDIO_PRSSTAT_WRTRANACT_MASK);
    return;
}

uint8_t SDIO_GetSectorCount(SDIO_TypeDef *sdio_t,
                             uint32_t *sector_cnt)
{
    uint8_t status = 0;
    uint32_t c_size = 0;
    uint32_t current_state = 0;


    // Get card state
    status = SDIO_S_SendCMDWithOutDAT(sdio_t, CMD13,
        SDIO_ActCardState_st.cardRCA_u16 << _CMD13_RCA_SHIFT);
    current_state = (SDIO->RESP0 & 0x1E00) >> 9;

    // If card is not in standby, put it in standby
    if(current_state != 3){
      // CMD7 with RCA 0x0000 to return card to Stand-By state (SDIO Physical Layer Spec page 50)
      status = SDIO_S_SendCMDWithOutDAT(sdio_t, CMD7,0);
    }

    // CMD9 to get the CSD contents
    status = SDIO_S_SendCMDWithOutDAT(sdio_t, CMD9,
        SDIO_ActCardState_st.cardRCA_u16 << _CMD9_RCA_SHIFT);

    // Copy the response to our local CSD buffer
    SDIO_GetR2(sdio_t, &csd_buf);

    // Determine the sector count from the CSD response
    switch( (csd_buf[0] & 0xC0000000) >> 30 ){ // CSD[127:126]

      case 0: // CSD Ver 1.0
        c_size = (csd_buf[1] & 0x3FF) << 2 | (csd_buf[2] & 0xC0000000) >> 30; // CSD[73:62]
        // elm-chan stm32 example calculates it this way:
        // n = READ_BL_LEN + C_SIZE_MULT + 2
        // *sector_cnt = (c_size + 1) << (n - 9);
        uint32_t n = ((csd_buf[1] & 0x000F0000) >> 16) + ((csd_buf[2] & 0x38000) >> 15) + 2;
        *sector_cnt = (c_size + 1) << (n - 9);
        break;

      case 1: // CSD Ver 2.0
        c_size = (csd_buf[1] & 0x1F) << 16 | (csd_buf[2] & 0xFFFF0000) >> 16;
        *sector_cnt = (c_size + 1) << 10; // memory capacity = (c_size + 1) * 1024
        break;

      default:
        return -1;
    }

    // Return card to transfer mode if it started there
    if(current_state != 3){
        status = SDIO_S_SendCMDWithOutDAT(sdio_t, CMD7,
                                          SDIO_ActCardState_st.cardRCA_u16 << _CMD13_RCA_SHIFT);
        status = SDIO_S_SendCMDWithOutDAT(sdio_t, CMD13,
            SDIO_ActCardState_st.cardRCA_u16 << _CMD13_RCA_SHIFT);
        current_state = (SDIO->RESP0 & 0x1E00) >> 9;

    }

    return status;
}

uint8_t SDIO_GetBlockSize(SDIO_TypeDef *sdio_t,
                             uint32_t *block_sz)
{
  volatile uint8_t status = 0;
  uint32_t current_state = 0;
  uint32_t tmpReg_u32 = 0;

  // Get card state
  status = SDIO_S_SendCMDWithOutDAT(sdio_t, CMD13,
      SDIO_ActCardState_st.cardRCA_u16 << _CMD13_RCA_SHIFT);
  current_state = (sdio_t->RESP0 & 0x1E00) >> 9;

  // If card is not in transfer mode, put it in transfer mode
  if(current_state != 4){
      status = SDIO_S_SendCMDWithOutDAT(sdio_t, CMD7,
                                        SDIO_ActCardState_st.cardRCA_u16 << _CMD7_RCA_SHIFT);
  }

  // Send CMD55
  SDIO_S_SendCMDWithOutDAT(sdio_t, CMD55,
      SDIO_ActCardState_st.cardRCA_u16 << _CMD55_RCA_SHIFT);


  ////// SEND ACMD13 //////

  // Disable DAT CRC check (broken for this command?)
  // TODO: Comprehensive error check overhaul, ensure this line doesn't mess things up
  sdio_t->IFENC &= ~(SDIO_IFENC_DATCRCERREN);

  // Issue CMD A13
  SDIO_IssueCMD(sdio_t, ACMD13, 0, 1);

  // 10. wait for Buffer Read Ready int
  SDIO_S_WaitForFlag( (_SDIO_IFCR_BFRRDRDY_MASK | SDIO_IFCR_DATTOUTERR) , SD_SPINLOOP_MODE);

  // 11. clear previous Buffer Read Ready Int
  SDIO_S_ClearFlag(_SDIO_IFCR_BFRRDRDY_MASK | SDIO_IFCR_DATTOUTERR);

  // 12. Set Block Data
//  localbuffptr_pu32 = &sdio_t->BUFDATPORT;
  for (int i = 0; i < SD_STATUS_BUF_LEN; i++)
  {
    uint32_t tmpData_u32 = sdio_t->BUFDATPORT;
    // 2025 07 01 LW: Byte swap to get the bits in the correct order
    sd_status_buf[i] = ((tmpData_u32 & 0xFF000000) >> 24) |
                  ((tmpData_u32 & 0x00FF0000) >> 8)  |
                  ((tmpData_u32 & 0x0000FF00) << 8)  |
                  ((tmpData_u32 & 0x000000FF) << 24);
  }


  // Issue CMD12 to terminate the ACMD13 data transfer
  status = SDIO_S_SendCMDWithOutDAT(sdio_t, CMD12,
      SDIO_ActCardState_st.cardRCA_u16 << _CMD13_RCA_SHIFT);

  // 19. Wait for transfer completed int
  SDIO_S_WaitForFlag( (_SDIO_IFCR_TRANCOM_MASK) , SD_SPINLOOP_MODE);

  // 20. CLear Transfer completed status
  SDIO_S_ClearFlag(_SDIO_IFCR_TRANCOM_MASK);


  // Determine erase block size
  uint32_t erase_size = sd_status_buf[2] & 0x0000FFFF;
  *block_sz = erase_size;


  return status;
}

void SDIO_GetR2(SDIO_TypeDef *sdio_t, uint32_t *buf)
{
  buf[0] = (sdio_t->RESP6 << 8) | (sdio_t->RESP4 >> 24);
  buf[1] = (sdio_t->RESP4 << 8) | (sdio_t->RESP2 >> 24);
  buf[2] = (sdio_t->RESP2 << 8) | (sdio_t->RESP0 >> 24);
  buf[3] = (sdio_t->RESP0 << 8);
}

// 2025 12 08 LW: Standardized function to wait for SD card responses
uint8_t SDIO_S_WaitForFlag(uint32_t ien_mask, SD_WaitType_t use_waitloop)
{

  // Initial fast check
  if(SDIO->IFCR & ien_mask)
  {
    return 1;
  }

  // If we are not using a waitloop, set up interrupts and wait for them
  if(use_waitloop == 0)
  {
//    calling_task_handle = xTaskGetCurrentTaskHandle();
    calling_task_tcb = OSTCBCurPtr;
    SDIO->IEN = ien_mask;
    NVIC_EnableIRQ(SDIO_IRQn);
//    uint8_t got_interrupt = xTaskNotifyWait(0, 0, NULL, MOD_SD_ISR_TIMEOUT_TICKS);
//    SEGGER_SYSVIEW_PrintfHost("got int: %d", got_interrupt);
//    return got_interrupt;
    OS_SEM_CTR ctr = OSTaskSemPend((OSTimeTickRateHzGet(NULL) / 10),
                                   OS_OPT_PEND_BLOCKING,
                                   DEF_NULL,
                                   &err);
    return (ctr == 0 ? 1 : 0);
  }
  // Otherwise, execute a spin-wait
  else
  {
    while(!(SDIO->IFCR & ien_mask));
    return 1;
  }
}


void SDIO_S_ClearFlag(uint32_t flag_mask)
{
  while(SDIO->IFCR & flag_mask)
  {
    // 2025 12 09 LW: For SDIO->IFCR, "|=" clears the whole register. "=" lets us clear only specific bits.
    SDIO->IFCR = flag_mask;
  }
}


void SDIO_IRQHandler(void){

  NVIC_DisableIRQ(SDIO_IRQn);
  SDIO->IEN = 0;

//  int_flags = SDIO->IFCR;
//
//  SDIO->IFCR = int_flags;

//  SEGGER_SYSVIEW_PrintfHost("0x%08x", SDIO->IFCR);
  //printf("%#034x\r\n", int_flags);

//  if((int_flags & _SDIO_IFCR_CMDCOM_MASK) || (int_flags & _SDIO_IFCR_ERRINT_MASK))
//  {
//      BaseType_t xYieldRequired;
//      xYieldRequired = xTaskResumeFromISR((TaskHandle_t) mod_sd_task_handle);
//      printf("%d\r\n", xYieldRequired);

//  vTaskNotifyGiveFromISR((TaskHandle_t) calling_task_handle, NULL);
  OSTaskSemPost(calling_task_tcb,
                OS_OPT_POST_NONE,
                &err);

//  }

//  NVIC_EnableIRQ(SDIO_IRQn);

  //portYIELD_FROM_ISR();

}
