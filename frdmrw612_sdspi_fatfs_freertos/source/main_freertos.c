/*
 * SD card (SPI mode) + FatFs test on FRDM-RW612, MCUXpresso SDK, FreeRTOS.
 *
 * Tasks (each on its own priority level):
 *   sd_reader   prio 3  reads hello.txt while the throughput test runs (FatFs re-entrancy)
 *   sd_test     prio 2  runs the shared test sequence and the hot-plug monitor
 *   background  prio 1  busy counter: measures the CPU time the SD task leaves to others
 *
 * sd_reader must outrank sd_test. A waiter below the mutex holder never gets to run between the
 * holder's back-to-back f_read() calls, so it would be starved and FatFs locking would go untested.
 *
 * The test sequence and its options (APP_*) are in the bare-metal project's sd_test.c / sd_test.h.
 */
#include "FreeRTOS.h"
#include "task.h"

#include "fsl_debug_console.h"

#include "app.h"
#include "board.h"
#include "sd_test.h"

#define SD_TASK_STACK_WORDS         (8192U / sizeof(StackType_t))
#define READER_TASK_STACK_WORDS     (4096U / sizeof(StackType_t))
#define BACKGROUND_TASK_STACK_WORDS (1024U / sizeof(StackType_t))

#define SD_TASK_PRIORITY         (tskIDLE_PRIORITY + 2U)
#define READER_TASK_PRIORITY     (tskIDLE_PRIORITY + 3U)
#define BACKGROUND_TASK_PRIORITY (tskIDLE_PRIORITY + 1U)

static void sd_task(void *arg)
{
    (void)arg;

    SD_TestRun();

    /* Only reached with APP_HOTPLUG == 0 or APP_CMD0_TEST_ONLY == 1. */
    vTaskSuspend(NULL);
}

static void background_task(void *arg)
{
    (void)arg;

    for (;;)
    {
        g_sdTestBackgroundCounter++;
    }
}

int main(void)
{
    BOARD_InitHardware();

    if ((xTaskCreate(sd_task, "sd_test", SD_TASK_STACK_WORDS, NULL, SD_TASK_PRIORITY, NULL) != pdPASS) ||
        (xTaskCreate(SD_TestConcurrentReaderTask, "sd_reader", READER_TASK_STACK_WORDS, NULL, READER_TASK_PRIORITY,
                     NULL) != pdPASS) ||
        (xTaskCreate(background_task, "background", BACKGROUND_TASK_STACK_WORDS, NULL, BACKGROUND_TASK_PRIORITY,
                     NULL) != pdPASS))
    {
        PRINTF("Task creation failed (heap too small?)\r\n");
        while (1)
        {
        }
    }

    vTaskStartScheduler();

    while (1)
    {
    }
}
