/*
 * SD card (SPI mode) + FatFs test sequence, shared by the bare-metal and FreeRTOS builds.
 *
 * Wiring: see source/sdspi/sd_spi_port.h or readme.md.
 */
#include "sd_test.h"

#include <stdbool.h>
#include <string.h>

#include "fsl_debug_console.h"
#include "fsl_device_registers.h"

#include "ff.h"
#include "diskio.h"
#include "fsl_sdspi_disk.h"
#include "sd_spi.h"

#if SD_SPI_PORT_FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#endif

/*******************************************************************************
 * Definitions
 ******************************************************************************/
#define APP_DRIVE      "4:" /* SDSPIDISK is physical drive 4 in the SDK's diskio.h */
#define APP_HELLO_PATH APP_DRIVE "/hello.txt"
#define APP_SPEED_PATH APP_DRIVE "/speed.bin"
#define APP_HELLO_TEXT "Hello from MCUXpresso SDK on FRDM-RW612!\n"

#define APP_SPEED_FILE_SIZE (1024U * 1024U)
#define APP_IO_CHUNK        (32U * 1024U)

/*******************************************************************************
 * Variables
 ******************************************************************************/
static FATFS s_fs;
static FIL s_file;
/* Word aligned: FatFs passes it straight to the disk layer. Also used as f_mkfs work area. */
static uint8_t s_ioBuffer[APP_IO_CHUNK] __attribute__((aligned(4)));

#if SD_SPI_PORT_FREERTOS
volatile uint32_t g_sdTestBackgroundCounter;

static SemaphoreHandle_t s_readerGate; /* held by the reader task while it touches the volume */
static volatile bool s_readerRun;
static volatile uint32_t s_readerOk;
static volatile uint32_t s_readerErr;
static uint32_t s_backgroundPerMs; /* background counter rate with the SD task asleep */
#endif

/*******************************************************************************
 * Code
 ******************************************************************************/
static const char *fresult_name(FRESULT fr)
{
    static const char *const names[] = {
        "FR_OK",           "FR_DISK_ERR",       "FR_INT_ERR",          "FR_NOT_READY",           "FR_NO_FILE",
        "FR_NO_PATH",      "FR_INVALID_NAME",   "FR_DENIED",           "FR_EXIST",               "FR_INVALID_OBJECT",
        "FR_WRITE_PROTECTED", "FR_INVALID_DRIVE", "FR_NOT_ENABLED",    "FR_NO_FILESYSTEM",       "FR_MKFS_ABORTED",
        "FR_TIMEOUT",      "FR_LOCKED",         "FR_NOT_ENOUGH_CORE",  "FR_TOO_MANY_OPEN_FILES", "FR_INVALID_PARAMETER",
    };

    return ((uint32_t)fr < (sizeof(names) / sizeof(names[0]))) ? names[(uint32_t)fr] : "FR_?";
}

/* Print a failed FatFs call together with the driver status behind it. */
static void report_fresult(const char *what, FRESULT fr)
{
    PRINTF("%s failed: %s (%d)", what, fresult_name(fr), (int)fr);
    if ((fr == FR_DISK_ERR) || (fr == FR_NOT_READY))
    {
        PRINTF(", SD driver: %s", SD_StatusString(sdspi_disk_last_status()));
    }
    PRINTF("\r\n");
}

static void cmd0_test(void)
{
    static const uint8_t cmd0[6] = {0x40U, 0x00U, 0x00U, 0x00U, 0x00U, 0x95U};
    uint8_t rx[8];
    uint8_t r1 = 0xFFU;
    uint32_t attempt;
    uint32_t n;
    uint32_t i;

    SD_SPI_PortInit();
    PRINTF("Flexcomm1 clock: %u Hz, SPI clock: %u Hz\r\n", SD_SPI_PortGetSourceClock(),
           SD_SPI_PortSetFrequency(SD_SPI_INIT_HZ));
    SD_SPI_PortDelayMs(10U);

    for (attempt = 1U; attempt <= 3U; attempt++)
    {
        /* >= 74 clocks with CS high, then CMD0 with CS low. */
        SD_SPI_PortSelect(false);
        (void)SD_SPI_PortExchange(NULL, NULL, 10U);
        SD_SPI_PortSelect(true);
        (void)SD_SPI_PortExchange(cmd0, NULL, sizeof(cmd0));

        for (n = 0U; n < sizeof(rx);)
        {
            r1      = SD_SPI_PortXferByte(0xFFU);
            rx[n++] = r1;
            if (r1 != 0xFFU)
            {
                break;
            }
        }
        SD_SPI_PortSelect(false);
        (void)SD_SPI_PortXferByte(0xFFU);

        PRINTF("CMD0 attempt %u, rx:", attempt);
        for (i = 0U; i < n; i++)
        {
            PRINTF(" %02X", rx[i]);
        }
        PRINTF("\r\n");

        if (r1 == 0x01U)
        {
            PRINTF("CMD0 OK: R1 = 0x01 (card is in idle state)\r\n");
            return;
        }
        SD_SPI_PortDelayMs(100U);
    }

    PRINTF("CMD0 FAILED, last R1 = 0x%02X (expected 0x01)\r\n", r1);
}

static bool card_init_and_report(void)
{
    const sd_card_t *card = sdspi_disk_card();
    const uint8_t *cid    = card->cid;

    sdspi_disk_set_max_frequency(APP_SD_MAX_HZ);
    if (disk_initialize(SDSPIDISK) != 0U)
    {
        PRINTF("Card init failed: %s (CMD%u, R1=0x%02X)\r\n", SD_StatusString(sdspi_disk_last_status()),
               card->lastCmd, card->lastR1);
        return false;
    }

    PRINTF("Card type : %s\r\n", SD_TypeString(card->type));
    PRINTF("Capacity  : %u MB (%u sectors)\r\n", card->sectorCount / 2048U, card->sectorCount);
    PRINTF("OCR       : 0x%08X\r\n", card->ocr);
    PRINTF("CID       : MID=0x%02X OID=%c%c PNM=%c%c%c%c%c PRV=%u.%u PSN=0x%02X%02X%02X%02X MDT=%u/%02u\r\n", cid[0],
           cid[1], cid[2], cid[3], cid[4], cid[5], cid[6], cid[7], (uint32_t)cid[8] >> 4, (uint32_t)cid[8] & 0x0FU,
           cid[9], cid[10], cid[11], cid[12], 2000U + ((((uint32_t)cid[13] & 0x0FU) << 4) | ((uint32_t)cid[14] >> 4)),
           (uint32_t)cid[14] & 0x0FU);
    PRINTF("SPI clock : %u Hz\r\n", card->clockHz);

    return true;
}

static bool mount_volume(void)
{
    FRESULT fr = f_mount(&s_fs, APP_DRIVE, 1U);

#if APP_FORMAT_IF_NO_FILESYSTEM
    if (fr == FR_NO_FILESYSTEM)
    {
        static const MKFS_PARM opt = {FM_FAT | FM_FAT32, 0U, 0U, 0U, 0U};

        PRINTF("No file system, formatting...\r\n");
        fr = f_mkfs(APP_DRIVE, &opt, s_ioBuffer, sizeof(s_ioBuffer));
        if (fr != FR_OK)
        {
            report_fresult("f_mkfs", fr);
            return false;
        }
        fr = f_mount(&s_fs, APP_DRIVE, 1U);
    }
#endif

    if (fr != FR_OK)
    {
        report_fresult("f_mount", fr);
        if (fr == FR_NO_FILESYSTEM)
        {
            PRINTF("Format the card as FAT32 on a PC, or set APP_FORMAT_IF_NO_FILESYSTEM to 1.\r\n");
        }
        return false;
    }

    PRINTF("Mounted %s, FAT type %u\r\n", APP_DRIVE, (uint32_t)s_fs.fs_type);
    return true;
}

static bool hello_write(void)
{
    FRESULT fr;
    UINT written = 0U;
    UINT len     = (UINT)strlen(APP_HELLO_TEXT);

    fr = f_open(&s_file, APP_HELLO_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        report_fresult("f_open(hello.txt, write)", fr);
        return false;
    }

    fr = f_write(&s_file, APP_HELLO_TEXT, len, &written);
    if (fr == FR_OK)
    {
        fr = f_sync(&s_file);
    }
    (void)f_close(&s_file);

    if ((fr != FR_OK) || (written != len))
    {
        report_fresult("f_write(hello.txt)", fr);
        return false;
    }

    PRINTF("Wrote %u bytes to hello.txt\r\n", written);
    return true;
}

static bool hello_read_back(void)
{
    FRESULT fr;
    UINT got = 0U;
    char text[64];

    fr = f_open(&s_file, APP_HELLO_PATH, FA_READ);
    if (fr != FR_OK)
    {
        report_fresult("f_open(hello.txt, read)", fr);
        return false;
    }

    fr = f_read(&s_file, text, sizeof(text) - 1U, &got);
    (void)f_close(&s_file);
    if (fr != FR_OK)
    {
        report_fresult("f_read(hello.txt)", fr);
        return false;
    }
    text[got] = '\0';

    PRINTF("Read %u bytes from hello.txt: %s\r", got, text);
    if (strcmp(text, APP_HELLO_TEXT) != 0)
    {
        PRINTF("Compare: MISMATCH\r\n");
        return false;
    }
    PRINTF("Compare: OK\r\n");
    return true;
}

static bool list_root(void)
{
    DIR dir;
    FILINFO info;
    FRESULT fr;

    fr = f_opendir(&dir, APP_DRIVE "/");
    if (fr != FR_OK)
    {
        report_fresult("f_opendir", fr);
        return false;
    }

    PRINTF("Root directory:\r\n");
    for (;;)
    {
        fr = f_readdir(&dir, &info);
        if ((fr != FR_OK) || (info.fname[0] == '\0'))
        {
            break;
        }
        if ((info.fattrib & AM_DIR) != 0U)
        {
            PRINTF("  <DIR>       %s\r\n", info.fname);
        }
        else
        {
            PRINTF("  %10u  %s\r\n", (uint32_t)info.fsize, info.fname);
        }
    }
    (void)f_closedir(&dir);

    if (fr != FR_OK)
    {
        report_fresult("f_readdir", fr);
        return false;
    }
    return true;
}

/* ---------------------------------------------------------------------------------------------- */
/* FreeRTOS extras: a second task reading the volume, and a measure of the CPU left to other tasks  */
/* ---------------------------------------------------------------------------------------------- */
#if SD_SPI_PORT_FREERTOS
void SD_TestConcurrentReaderTask(void *arg)
{
    static FIL file; /* task-private file object, kept off the stack */
    char text[64];
    UINT got;
    FRESULT fr;

    (void)arg;

    for (;;)
    {
        if ((s_readerGate != NULL) && (xSemaphoreTake(s_readerGate, portMAX_DELAY) == pdTRUE))
        {
            if (s_readerRun)
            {
                got = 0U;
                fr  = f_open(&file, APP_HELLO_PATH, FA_READ);
                if (fr == FR_OK)
                {
                    fr = f_read(&file, text, sizeof(text) - 1U, &got);
                    (void)f_close(&file);
                }
                text[got] = '\0';
                if ((fr == FR_OK) && (strcmp(text, APP_HELLO_TEXT) == 0))
                {
                    s_readerOk++;
                }
                else
                {
                    s_readerErr++;
                }
            }
            (void)xSemaphoreGive(s_readerGate);
        }
        vTaskDelay(pdMS_TO_TICKS(5U));
    }
}

static void rtos_extras_init(void)
{
    uint32_t before;

    if (s_readerGate == NULL)
    {
        s_readerGate = xSemaphoreCreateMutex();
    }
    if (s_backgroundPerMs == 0U)
    {
        /* With this task asleep the background task gets the whole CPU: that is the 100 % reference. */
        before = g_sdTestBackgroundCounter;
        vTaskDelay(pdMS_TO_TICKS(100U));
        s_backgroundPerMs = (g_sdTestBackgroundCounter - before) / 100U;
    }
}

static void reader_start(void)
{
    s_readerOk  = 0U;
    s_readerErr = 0U;
    s_readerRun = true;
}

/* Returns once the reader task is guaranteed to be off the volume. */
static void reader_stop(void)
{
    s_readerRun = false;
    if (s_readerGate != NULL)
    {
        (void)xSemaphoreTake(s_readerGate, portMAX_DELAY);
        (void)xSemaphoreGive(s_readerGate);
    }
}

static void rtos_report(uint32_t backgroundDelta, uint64_t us)
{
    uint32_t ms = (uint32_t)(us / 1000U);

    if ((s_backgroundPerMs != 0U) && (ms != 0U))
    {
        uint32_t share = (uint32_t)(((uint64_t)backgroundDelta * 100U) / ((uint64_t)s_backgroundPerMs * ms));

        PRINTF("         CPU left to a lower-priority task: %u %%\r\n", share);
    }
}
#endif /* SD_SPI_PORT_FREERTOS */

/* ---------------------------------------------------------------------------------------------- */
/* Throughput                                                                                       */
/* ---------------------------------------------------------------------------------------------- */
#if APP_THROUGHPUT_TEST
static uint8_t pattern_byte(uint32_t offset)
{
    return (uint8_t)(offset ^ (offset >> 8) ^ (offset >> 16));
}

static uint32_t kb_per_s(uint32_t bytes, uint64_t us)
{
    return (us == 0U) ? 0U : (uint32_t)(((uint64_t)bytes * 1000000ULL) / (us * 1024ULL));
}

#if SD_SPI_PROFILE
static void profile_line(const char *name, uint64_t cycles)
{
    uint32_t t = (uint32_t)((cycles * 10000ULL) / SystemCoreClock); /* tenths of a millisecond */

    PRINTF("  %s %u.%u ms\r\n", name, t / 10U, t % 10U);
}

static void profile_report(uint64_t fileUs)
{
    const sd_profile_t *p = &g_sdProfile;

    PRINTF("Read profile: %u driver calls, %u blocks, %u token poll bytes (%u per block)\r\n", p->readCalls,
           p->readBlocks, p->tokenPolls, (p->readBlocks != 0U) ? (p->tokenPolls / p->readBlocks) : 0U);
    PRINTF("  f_read total    %u.%u ms\r\n", (uint32_t)(fileUs / 1000U), (uint32_t)((fileUs / 100U) % 10U));
    profile_line("driver total   ", p->readTotal);
    profile_line("  command      ", p->readCommand);
    profile_line("  token wait   ", p->readToken);
    profile_line("  data 512 B   ", p->readData);
    profile_line("  crc 2 B      ", p->readCrc);
    profile_line("  stop + desel ", p->readStop);
}
#endif

/* Write a 1 MB file, read it back and verify. Each chunk is timed on its own (32-bit cycle counter). */
static bool throughput_pass(const char *mode)
{
    FRESULT fr;
    UINT done;
    uint64_t us = 0U;
    uint32_t offset;
    uint32_t start;
    uint32_t i;
#if SD_SPI_PORT_FREERTOS
    uint32_t background = 0U;
    uint32_t mark;
#endif

    fr = f_open(&s_file, APP_SPEED_PATH, FA_CREATE_ALWAYS | FA_WRITE);
    if (fr != FR_OK)
    {
        report_fresult("f_open(speed.bin, write)", fr);
        return false;
    }
    for (offset = 0U; offset < APP_SPEED_FILE_SIZE; offset += APP_IO_CHUNK)
    {
        for (i = 0U; i < APP_IO_CHUNK; i++)
        {
            s_ioBuffer[i] = pattern_byte(offset + i);
        }
#if SD_SPI_PORT_FREERTOS
        mark = g_sdTestBackgroundCounter;
#endif
        start = SD_SPI_PortTimeStart();
        fr    = f_write(&s_file, s_ioBuffer, APP_IO_CHUNK, &done);
        us += SD_SPI_PortElapsedUs(start);
#if SD_SPI_PORT_FREERTOS
        background += g_sdTestBackgroundCounter - mark;
#endif
        if ((fr != FR_OK) || (done != APP_IO_CHUNK))
        {
            report_fresult("f_write(speed.bin)", fr);
            (void)f_close(&s_file);
            return false;
        }
    }
    start = SD_SPI_PortTimeStart();
    fr    = f_close(&s_file);
    us += SD_SPI_PortElapsedUs(start);
    if (fr != FR_OK)
    {
        report_fresult("f_close(speed.bin)", fr);
        return false;
    }
    PRINTF("[%s] Write: %u KB in %u ms = %u KB/s\r\n", mode, APP_SPEED_FILE_SIZE / 1024U, (uint32_t)(us / 1000U),
           kb_per_s(APP_SPEED_FILE_SIZE, us));
#if SD_SPI_PORT_FREERTOS
    rtos_report(background, us);
    background = 0U;
#endif

    us = 0U;
    fr = f_open(&s_file, APP_SPEED_PATH, FA_READ);
    if (fr != FR_OK)
    {
        report_fresult("f_open(speed.bin, read)", fr);
        return false;
    }
#if SD_SPI_PROFILE
    (void)memset(&g_sdProfile, 0, sizeof(g_sdProfile));
#endif
    for (offset = 0U; offset < APP_SPEED_FILE_SIZE; offset += APP_IO_CHUNK)
    {
#if SD_SPI_PORT_FREERTOS
        mark = g_sdTestBackgroundCounter;
#endif
        start = SD_SPI_PortTimeStart();
        fr    = f_read(&s_file, s_ioBuffer, APP_IO_CHUNK, &done);
        us += SD_SPI_PortElapsedUs(start);
#if SD_SPI_PORT_FREERTOS
        background += g_sdTestBackgroundCounter - mark;
#endif
        if ((fr != FR_OK) || (done != APP_IO_CHUNK))
        {
            report_fresult("f_read(speed.bin)", fr);
            (void)f_close(&s_file);
            return false;
        }
        for (i = 0U; i < APP_IO_CHUNK; i++)
        {
            if (s_ioBuffer[i] != pattern_byte(offset + i))
            {
                PRINTF("[%s] Verify: MISMATCH at offset %u\r\n", mode, offset + i);
                (void)f_close(&s_file);
                return false;
            }
        }
    }
    (void)f_close(&s_file);
    PRINTF("[%s] Read : %u KB in %u ms = %u KB/s\r\n", mode, APP_SPEED_FILE_SIZE / 1024U, (uint32_t)(us / 1000U),
           kb_per_s(APP_SPEED_FILE_SIZE, us));
#if SD_SPI_PORT_FREERTOS
    rtos_report(background, us);
#endif
#if SD_SPI_PROFILE
    profile_report(us);
#endif
    PRINTF("[%s] Verify: OK\r\n", mode);

    return true;
}

static bool throughput_mode(const char *mode, bool useDma)
{
    bool ok;

    SD_SPI_PortSetDmaEnabled(useDma);
#if SD_SPI_PORT_FREERTOS
    reader_start();
#endif
    ok = throughput_pass(mode);
#if SD_SPI_PORT_FREERTOS
    reader_stop();
    PRINTF("[%s] Concurrent reader task: %u reads OK, %u errors\r\n", mode, s_readerOk, s_readerErr);
    /* Zero reads means the reader never got in: the locking was not exercised, so do not report a pass. */
    if ((s_readerErr != 0U) || (s_readerOk == 0U))
    {
        ok = false;
    }
#endif
    return ok;
}

static bool throughput_test(void)
{
    bool ok = throughput_mode("polling", false);

#if SD_SPI_PORT_DMA
    if (ok)
    {
        ok = throughput_mode("DMA", true);
    }
#endif
    return ok;
}
#endif /* APP_THROUGHPUT_TEST */

/* ---------------------------------------------------------------------------------------------- */

static bool run_test(void)
{
    bool ok;

    if (!card_init_and_report())
    {
        return false;
    }
    if (!mount_volume())
    {
        return false;
    }

    ok = hello_write() && hello_read_back();
#if APP_THROUGHPUT_TEST
    ok = ok && throughput_test();
#endif
    ok = ok && list_root();

    (void)f_mount(NULL, APP_DRIVE, 0U);
    return ok;
}

static void run_and_report(void)
{
    if (run_test())
    {
        PRINTF("Test completed.\r\n");
    }
    else
    {
        PRINTF("Test FAILED.\r\n");
    }
}

#if APP_HOTPLUG
/*
 * Watch the slot forever. Removal: unmount and forget the card. Insertion: full card init and test.
 * Without a card-detect switch an empty slot is polled by trying to initialise a card (quietly).
 */
static void hotplug_monitor(void)
{
    bool present = sdspi_disk_card()->initialized;

    PRINTF("Hot-plug monitor: %s. Remove or insert the card.\r\n",
           (SD_SPI_PortCardDetect() >= 0) ? "card-detect switch" : "no card-detect switch, probing with CMD13");

    for (;;)
    {
        SD_SPI_PortDelayMs(APP_HOTPLUG_POLL_MS);

        if (present)
        {
            if (!sdspi_disk_present())
            {
                PRINTF("\r\nCard removed.\r\n");
                (void)f_mount(NULL, APP_DRIVE, 0U);
                sdspi_disk_deinit();
                present = false;
            }
        }
        else
        {
            bool found;

            if (SD_SPI_PortCardDetect() == 0)
            {
                continue; /* the switch says the slot is empty */
            }

            SD_SetLogEnabled(false);
            sdspi_disk_set_max_frequency(APP_SD_MAX_HZ);
            found = (disk_initialize(SDSPIDISK) == 0U);
            SD_SetLogEnabled(true);

            if (found)
            {
                PRINTF("\r\nCard inserted.\r\n");
                present = true;
                run_and_report();
                /* The card may have been pulled during the test. */
                present = sdspi_disk_card()->initialized && sdspi_disk_present();
                if (!present)
                {
                    sdspi_disk_deinit();
                }
            }
        }
    }
}
#endif /* APP_HOTPLUG */

void SD_TestRun(void)
{
    PRINTF("\r\n*** SD Card SPI Test on FRDM-RW612 (MCUXpresso SDK) ***\r\n");
#if SD_SPI_PORT_FREERTOS
    PRINTF("FreeRTOS build, DMA support: %s\r\n", (SD_SPI_PORT_DMA != 0) ? "yes" : "no");
    rtos_extras_init();
#else
    PRINTF("Bare-metal build, DMA support: %s\r\n", (SD_SPI_PORT_DMA != 0) ? "yes" : "no");
#endif

    if (APP_CMD0_TEST_ONLY != 0)
    {
        cmd0_test();
        return;
    }

    run_and_report();

#if APP_HOTPLUG
    hotplug_monitor();
#endif
}
