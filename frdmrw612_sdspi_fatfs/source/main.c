/*
 * SD card (SPI mode) + FatFs test on FRDM-RW612, MCUXpresso SDK, bare metal.
 *
 * The test sequence and its options (APP_*) are in sd_test.c / sd_test.h.
 */
#include "app.h"
#include "board.h"
#include "sd_test.h"

int main(void)
{
    BOARD_InitHardware();

    SD_TestRun();

    while (1)
    {
    }
}
