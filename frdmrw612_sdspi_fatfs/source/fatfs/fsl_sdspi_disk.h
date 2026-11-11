/*
 * FatFs disk glue for the project's SD-SPI driver (source/sdspi).
 *
 * The SDK's diskio.c dispatches physical drive SDSPIDISK to the sdspi_disk_*() functions declared in
 * "fsl_sdspi_disk.h". The SDK's own implementation of that file is written for the Kinetis DSPI driver
 * and the sdmmc middleware, neither of which exists for RW612, so the project provides this one instead.
 * The file name must stay as it is: diskio.c includes it by name.
 */
#ifndef FSL_SDSPI_DISK_H_
#define FSL_SDSPI_DISK_H_

#include <stdbool.h>

#include "ff.h"
#include "diskio.h"
#include "sd_spi.h"

#if defined(__cplusplus)
extern "C" {
#endif

DSTATUS sdspi_disk_initialize(BYTE pdrv);
DSTATUS sdspi_disk_status(BYTE pdrv);
DRESULT sdspi_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
DRESULT sdspi_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
DRESULT sdspi_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

/* Card behind the disk, for printing type / capacity / OCR / CID. */
const sd_card_t *sdspi_disk_card(void);

/* Result of the last driver call, for diagnostics after a FatFs error. */
sd_status_t sdspi_disk_last_status(void);

/* Hot-plug: forget the card after removal; disk_initialize() then runs a full card init again. */
void sdspi_disk_deinit(void);

/* Card presence (card-detect switch if configured, otherwise CMD13 probe of an initialised card). */
bool sdspi_disk_present(void);

/* SPI clock limit used by the next sdspi_disk_initialize(). 0 = SD_SPI_DEFAULT_HZ. */
void sdspi_disk_set_max_frequency(uint32_t hz);

#if defined(__cplusplus)
}
#endif

#endif /* FSL_SDSPI_DISK_H_ */
