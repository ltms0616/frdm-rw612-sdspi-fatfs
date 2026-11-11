/*
 * SD card (SPI mode) + FatFs test sequence, shared by the bare-metal and FreeRTOS builds.
 */
#ifndef SD_TEST_H_
#define SD_TEST_H_

#include <stdint.h>

#include "sd_spi_port.h"

/*******************************************************************************
 * Test options
 ******************************************************************************/
/* 1: only run the raw CMD0 check (wiring bring-up), no card init, no file system. */
#ifndef APP_CMD0_TEST_ONLY
#define APP_CMD0_TEST_ONLY 0
#endif

/* 1: format the card (FAT/FAT32) when f_mount reports FR_NO_FILESYSTEM. THIS ERASES THE CARD. */
#ifndef APP_FORMAT_IF_NO_FILESYSTEM
#define APP_FORMAT_IF_NO_FILESYSTEM 0
#endif

/* 1: write and read back a 1 MB file and print KB/s (once per transfer mode). */
#ifndef APP_THROUGHPUT_TEST
#define APP_THROUGHPUT_TEST 1
#endif

/* SPI clock limit after card init. The actual rate is the next divider of the Flexcomm clock below this. */
#ifndef APP_SD_MAX_HZ
#define APP_SD_MAX_HZ 25000000U
#endif

/* 1: after the test keep watching the card; unmount on removal, re-run the test on insertion. */
#ifndef APP_HOTPLUG
#define APP_HOTPLUG 1
#endif
#define APP_HOTPLUG_POLL_MS 500U

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Print the banner and run the test sequence. With APP_HOTPLUG == 1 this never returns.
 * Bare metal: call from main(). FreeRTOS: call from a task with at least 8 KB of stack.
 */
void SD_TestRun(void);

#if SD_SPI_PORT_FREERTOS
/*
 * Task body: while the throughput test runs, keeps reading hello.txt from a second task to exercise
 * FatFs re-entrancy (FF_FS_REENTRANT = 1). Create it with at least 4 KB of stack.
 */
void SD_TestConcurrentReaderTask(void *arg);

/* Incremented by a low-priority busy task; shows how much CPU the SD task leaves to others. */
extern volatile uint32_t g_sdTestBackgroundCounter;
#endif

#if defined(__cplusplus)
}
#endif

#endif /* SD_TEST_H_ */
