/***************************************************************************//**
 * @file sdio.h
 * @brief SDIO handler library - initial
 * @version 0.0.1
 *******************************************************************************
 * # License
 * <b>Copyright 2016 Silicon Laboratories, Inc. www.silabs.com</b>
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
#ifndef __SDIO_H__
#define __SDIO_H__

#include "em_device.h"
#include "em_cmu.h"


#define MOD_SD_ISR_TIMEOUT_TICKS configTICK_RATE_HZ / 10

/**********************************************************
 * MACROs
 *********************************************************/
/* CMD0 */
#define CMD0 0UL // no argument - no response

/* CMD2 - ALL_SEND_CID */
#define CMD2 2UL // no argument; R2 response; bcr

/* CMD3 - Ask the card to publish new relative address */
#define CMD3 3UL // no argument - R6 response

/* CMD5 - Programs the DSR of all cards */
#define CMD5 5UL // no argument - R6 response

/* ACMD6 - CHANGE BUS WIDTH*/
#define ACMD6 0x40 + 6UL 						// R1 response; ac
#define _ACMD6_BUSWIDTH_SHIFT 0			// 00 - 1bit; 10 - 4bit
#define ACMD6_1BIT_BUSWIDTH_SET (0<<_ACMD6_BUSWIDTH_SHIFT)
#define ACMD6_4BIT_BUSWIDTH_SET (2<<_ACMD6_BUSWIDTH_SHIFT)

/* CMD7 - Card select/deselect - toggle between stdy and transfer */
#define CMD7 7UL // argument; ac; R1b
#define _CMD7_RCA_SHIFT 16 // 16 bit long

/* CMD8 - Interface Condition Command */
#define CMD8 8UL //	argument - applied v - R7 response
#define _CMD8_27V_36V_SHIFT 8

/* CMD9 - Send CSD (Card-Specific Data) */
#define CMD9 9UL  // R2 response
#define _CMD9_RCA_SHIFT 16 // 16 bit long

/* CMD10 - Send CID (Card Identification) */
#define CMD10 10UL
#define _CMD10_RCA_SHIFT 16 // 16 bit long

/* CMD11 - Voltage Switch to 1.8V */
#define CMD11 11UL // R1; ac; no argument

/* CMD12 Abort*/
#define CMD12 12UL // R1b; ac

/* CMD13 Send status/task */
#define CMD13 13UL // R1; ac
#define _CMD13_RCA_SHIFT 16 // 16 bit long
#define _CMD13_STATUSTASK_SHIFT 15 //  1 bit long
#define CMD13_SEND_STATUS (0<<_CMD15_STATUSTASK_SHIFT)
#define CMD13_TASK_STATUS (1<<_CMD15_STATUSTASK_SHIFT)

/* ACMD13 - Send SD Status */
#define ACMD13 0x40 + 13UL
//#define ACMD13_TFRMODE ((ACMD13 << _SDIO_TFRMODE_CMDINDEX_SHIFT) | SDIO_TFRMODE_DATPRESSEL_DATA | SDIO_TFRMODE_RESPTYPESEL_RESP48 | SDIO_TFRMODE_DATDIRSEL_ENABLE)

#define CMD16 16UL

/* CMD17 - Single Block Read*/
#define CMD17 17UL	// R1 response

// 2025 12 08 LW: Multiple Block Read
/* CMD18 - Multiple Block Read*/
#define CMD18 18UL

/* CMD24 - Single Block WRITE*/
#define CMD24 24UL	// R1 response

// 2025 12 08 LW: Multiple Block Write
/* CMD25 - Multiple Block WRITE*/
#define CMD25 25UL  // R1 responses

/* ACMD41 */
#define ACMD41 0x40 + 41UL // OCR w/out busy - R3 response
#define ACMD41_RESERVEDBIT31_SHIFT 31 	// 1 bit
#define _ACMD41_HCS_SHIFT 30 			// Host Capacity Support - 1 bit
#define _ACMD41_RESERVEDBIT29_SHIFT 29 	// 1 bit
#define _ACMD41_XPC_SHIFT 28 			// Maximum Power control - 1 bit
#define _ACMD41_RESERVEDBIT25_SHIFT 25 	// 3 bit
#define _ACMD41_S18R_SHIFT 24			// Send Request to switch 1.8V - 1 bit
#define _ACMD41_VW_SHIFT 0				// Voltage window register - 24 bit
#define _ACMD41_VW_30_31_SHIFT 18		// Voltage window register 3.0V - 3.1V
#define _ACMD41_VW_31_32_SHIFT 19		// Voltage window register 3.1V - 3.2V
#define _ACMD41_RESPONSE_BUSY_BIT_SHIFT 31

/* CMD55 */
#define CMD55 55UL						// R1 response; ac
#define _CMD55_RCA_SHIFT 16				// Relative Card Address - 16 bit
#define _CMD55_STUFFBITS_SHIFT 0		// Stuff Bits - 16 bit

/* CMD58 - Multiple Block Read */
#define CMD58 58UL // argument, R1, adtc

#define RCA_DEFAULT 0x0000


#define DEFLT_TFRMODE SDIO_TFRMODE_DMAEN_DISABLE | \
                      SDIO_TFRMODE_AUTOCMDEN_ACMDDISABLED | \
                      SDIO_TFRMODE_CMDTYPE_NORMAL

#define CMD12_TFRMODE SDIO_TFRMODE_DATPRESSEL_DATA

#define CMD17_TFRMODE SDIO_TFRMODE_DATDIRSEL_ENABLE | \
                      SDIO_TFRMODE_DATPRESSEL_DATA | \
                      SDIO_TFRMODE_DMAEN_ENABLE

#define CMD18_TFRMODE SDIO_TFRMODE_BLKCNTEN_ENABLE | \
                      SDIO_TFRMODE_AUTOCMDEN_ACMD12EN | \
                      SDIO_TFRMODE_DATDIRSEL_ENABLE | \
                      SDIO_TFRMODE_MULTSINGBLKSEL_MULTIBLK | \
                      SDIO_TFRMODE_DATPRESSEL_DATA | \
                      SDIO_TFRMODE_DMAEN_ENABLE

#define CMD24_TFRMODE SDIO_TFRMODE_DATPRESSEL_DATA | \
                      SDIO_TFRMODE_DMAEN_ENABLE

#define CMD25_TFRMODE SDIO_TFRMODE_BLKCNTEN_ENABLE | \
                      SDIO_TFRMODE_AUTOCMDEN_ACMD12EN | \
                      SDIO_TFRMODE_MULTSINGBLKSEL_MULTIBLK | \
                      SDIO_TFRMODE_DATPRESSEL_DATA | \
                      SDIO_TFRMODE_DMAEN_ENABLE

#define ACMD13_TFRMODE SDIO_TFRMODE_DATPRESSEL_DATA | \
                       SDIO_TFRMODE_DATDIRSEL_ENABLE


/**********************************************************
 * Global types
 *********************************************************/
typedef enum
{
  Not_SD_Card = 0,
  SDSC_Ver101_or_Ver110 = 1,
  SDSC_Ver200_or_Ver300 = 2,
  SDHC_SDXC = 3,
  SDIO_only = 4,
  Unusable_Card = 5
} SDIO_CardType_t;

typedef enum
{
  SD_OK = 0,
  SD_ERR,
  SD_CMDTOUTERR,
  SD_CMDCRCERR,
  SD_CMDENDBITERR,
  SD_CMDINDEXERR,
  SD_DATTOUTERR,
  SD_DATCRCERR,
  SD_DATENDBITERR,
  SD_ADMAERR
} SD_RES;

typedef enum
{
  SD_INTERRUPT_MODE = 0,
  SD_SPINLOOP_MODE
} SD_WaitType_t;

typedef enum
{
  SD_RESP_NONE = 0, // No response.
  SD_RESP_R1,       // R1: Normal response.   Start(1), Transmission(1), CMD Index(6), Card Status(32), CRC(7), End(1)
  SD_RESP_R1b,      // R1b: Normal w/ busy.
  SD_RESP_R2,       // R2: CID/CSD register.  Start(1), Transmission(1), Reserved(6), CID/CSD + CRC(127), End(1)
  SD_RESP_R3,       // R3: OCR register.      Start(1), Transmission(1), Reserved(6), OCR(32), Reserved(7), End(1)
  SD_RESP_R6,       // R6: Published RCA.     Start(1), Transmission(1), CMD Index(6), NewRCA(16), SmallStatus(16), CRC(7), End(1)
  SD_RESP_R7        // R7: Interface cond.    Start(1), Transmission(1), CMD Index(6), Reserved(20), VoltAccpt(4), ChkPtrn(8), CRC(7), End(1)
} SD_RespType_t;


#define ADMA2_ACT2  0x20
#define ADMA2_ACT1  0x10
#define ADMA2_INT   0x04
#define ADMA2_END   0x02
#define ADMA2_VLID  0x01

typedef struct
{
  uint16_t ctl;
  uint16_t len;
  uint32_t addr;
} SD_ADMA2_Desc_t, *SD_ADMA2_Desc_ptr_t;

/**********************************************************
 * Global prototypes
 *********************************************************/
uint8_t SDIO_Init(SDIO_TypeDef *sdio_t,
               uint32_t sdioFreq_u32,
               CMU_Clock_TypeDef mainClock_t);
SD_RES SDIO_WriteSingleBlock(SDIO_TypeDef *sdio_t,
                           uint32_t SD_dest_u32,
                           uint32_t* origin_pu32);
SD_RES SDIO_WriteMultipleBlocks(SDIO_TypeDef *sdio_t,
                           uint32_t SD_dest_u32,
                           uint32_t* origin_pu32,
                           uint32_t blocks);
SD_RES SDIO_ReadSingleBlock(SDIO_TypeDef *sdio_t,
                          uint32_t SD_dest_u32,
                          uint32_t* origin_pu32);
SD_RES SDIO_ReadMultipleBlocks(SDIO_TypeDef *sdio_t,
                          uint32_t SD_origin_u32,
                          uint32_t* destination_pu32,
                          uint32_t blocks);
SD_RES SDIO_IssueCMD(SDIO_TypeDef *sdio_t,
                      uint32_t cmd_u32,
                      uint32_t cmdArg_u32,
                      uint32_t blocks);
uint8_t SDIO_GetActCardStateType(void);

// 2025 04 28 LW: Adding functions to complete disk_ioctl stubs
void SDIO_WaitForWriteFinish(SDIO_TypeDef *sdio_t);

uint8_t SDIO_GetSectorCount(SDIO_TypeDef *sdio_t,
                             uint32_t *sector_cnt);

uint8_t SDIO_GetBlockSize(SDIO_TypeDef *sdio_t,
                             uint32_t *block_sz);



#endif //__SDIO_H__
