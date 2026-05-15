/*------------------------------------------------------------------------*/
/* Sample code of OS dependent controls for FatFs                         */
/* (C)ChaN, 2014
 *
 *
 * TODO:
 *     fatfs_mutex is a temporary solution, single volume only, would
 *     probably not work for say second SD card                                              */
/*------------------------------------------------------------------------*/


#include "ff.h"


#if _FS_REENTRANT

//#include "FreeRTOS.h"
//#include "semphr.h"
#include "os.h"
#include "stdlib.h"

#include "mod_som_config.h"


static RTOS_ERR err;
static OS_MUTEX fatfs_mutex; // Workaround Fix: ignore sobj and use this persistent global instead. Single volume only

/*------------------------------------------------------------------------*/
/* Create a Synchronization Object
/*------------------------------------------------------------------------*/
/* This function is called in f_mount() function to create a new
/  synchronization object, such as semaphore and mutex. When a 0 is returned,
/  the f_mount() function fails with FR_INT_ERR.
*/

int ff_cre_syncobj (	/* 1:Function succeeded, 0:Could not create the sync object */
	BYTE vol,			/* Corresponding volume (logical drive number) */
	_SYNC_t *sobj		/* Pointer to return the created sync object */
)
{
	int ret;

//	*sobj = xSemaphoreCreateMutex();	/* FreeRTOS */
//	ret = (int)(*sobj != NULL);

	*sobj = &fatfs_mutex;
	OSMutexCreate(*sobj,
	              "FATFS",
	              &err);
	ret = (err.Code == RTOS_ERR_NONE ? 1 : 0);

	return (ret);
}



/*------------------------------------------------------------------------*/
/* Delete a Synchronization Object                                        */
/*------------------------------------------------------------------------*/
/* This function is called in f_mount() function to delete a synchronization
/  object that created with ff_cre_syncobj() function. When a 0 is returned,
/  the f_mount() function fails with FR_INT_ERR.
*/

int ff_del_syncobj (	/* 1:Function succeeded, 0:Could not delete due to any error */
	_SYNC_t sobj		/* Sync object tied to the logical drive to be deleted */
)
{
	int ret;

//  vSemaphoreDelete(sobj);		/* FreeRTOS */
//	ret = 1;
	OSMutexDel(sobj,
	           OS_OPT_DEL_NO_PEND,
	           &err);
	ret = (err.Code == RTOS_ERR_NONE ? 1 : 0);

  return (ret);
}



/*------------------------------------------------------------------------*/
/* Request Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on entering file functions to lock the volume.
/  When a 0 is returned, the file function fails with FR_TIMEOUT.
*/

int ff_req_grant (	/* 1:Got a grant to access the volume, 0:Could not get a grant */
	_SYNC_t sobj	/* Sync object to wait */
)
{
	int ret;

//	ret = (int)(xSemaphoreTake(sobj, _FS_TIMEOUT) == pdTRUE);	/* FreeRTOS */

	OSMutexPend(sobj,
              _FS_TIMEOUT,
              OS_OPT_PEND_BLOCKING,
              DEF_NULL,
              &err);
  ret = (err.Code == RTOS_ERR_NONE ? 1 : 0);


// 2025 12 09 LW: When access is granted, enable SDCLK
#ifdef MOD_SOM_FATFS_SDCLK_POWER_SAVE
	if(ret)
  {
    SDIO->CLOCKCTRL |= SDIO_CLOCKCTRL_SDCLKEN;
  }
#endif

  return (ret);
}



/*------------------------------------------------------------------------*/
/* Release Grant to Access the Volume                                     */
/*------------------------------------------------------------------------*/
/* This function is called on leaving file functions to unlock the volume.
*/

void ff_rel_grant (
	_SYNC_t sobj	/* Sync object to be signaled */
)
{
	int ret;

//  ret = (int)xSemaphoreGive(sobj);	/* FreeRTOS */

  OSMutexPost(sobj,
              OS_OPT_POST_NONE,
              &err);
  ret = (err.Code == RTOS_ERR_NONE ? 1 : 0);


// 2025 12 09 LW: When access is relieved, disable SDCLK to reduce power consumption
#ifdef MOD_SOM_FATFS_SDCLK_POWER_SAVE
  if(ret)
  {
    SDIO->CLOCKCTRL &= ~(SDIO_CLOCKCTRL_SDCLKEN);
  }
#endif

}

#endif




#if _USE_LFN == 3	/* LFN with a working buffer on the heap */
/*------------------------------------------------------------------------*/
/* Allocate a memory block                                                */
/*------------------------------------------------------------------------*/
/* If a NULL is returned, the file function fails with FR_NOT_ENOUGH_CORE.
*/

void* ff_memalloc (	/* Returns pointer to the allocated memory block */
	UINT msize		/* Number of bytes to allocate */
)
{
//	return pvPortMalloc(msize);	/* Allocate a new memory block with POSIX API */
  return malloc(msize);
}


/*------------------------------------------------------------------------*/
/* Free a memory block                                                    */
/*------------------------------------------------------------------------*/

void ff_memfree (
	void* mblock	/* Pointer to the memory block to free */
)
{
//  vPortFree(mblock);	/* Discard the memory block with POSIX API */
  return free(mblock);
}

#endif
