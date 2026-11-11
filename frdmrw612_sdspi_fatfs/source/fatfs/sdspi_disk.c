/*
 * FatFs disk glue for the project's SD-SPI driver. See fsl_sdspi_disk.h.
 */
#include "fsl_sdspi_disk.h"

#include <stddef.h>

static sd_card_t s_card;
static sd_status_t s_lastStatus = kSD_ErrNotInit;
static uint32_t s_maxHz         = 0U;

const sd_card_t *sdspi_disk_card(void)
{
    return &s_card;
}

sd_status_t sdspi_disk_last_status(void)
{
    return s_lastStatus;
}

void sdspi_disk_set_max_frequency(uint32_t hz)
{
    s_maxHz = hz;
}

void sdspi_disk_deinit(void)
{
    SD_Deinit(&s_card);
    s_lastStatus = kSD_ErrNotInit;
}

bool sdspi_disk_present(void)
{
    return SD_IsPresent(&s_card);
}

DSTATUS sdspi_disk_initialize(BYTE pdrv)
{
    if (pdrv != SDSPIDISK)
    {
        return STA_NOINIT;
    }
    if (s_card.initialized)
    {
        return 0U;
    }

    s_lastStatus = SD_Init(&s_card, s_maxHz);

    return (s_lastStatus == kSD_Ok) ? 0U : STA_NOINIT;
}

DSTATUS sdspi_disk_status(BYTE pdrv)
{
    if ((pdrv != SDSPIDISK) || !s_card.initialized)
    {
        return STA_NOINIT;
    }
    return 0U;
}

DRESULT sdspi_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if ((pdrv != SDSPIDISK) || (buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }
    if (!s_card.initialized)
    {
        return RES_NOTRDY;
    }

    s_lastStatus = SD_ReadBlocks(&s_card, buff, (uint32_t)sector, count);

    return (s_lastStatus == kSD_Ok) ? RES_OK : RES_ERROR;
}

DRESULT sdspi_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if ((pdrv != SDSPIDISK) || (buff == NULL) || (count == 0U))
    {
        return RES_PARERR;
    }
    if (!s_card.initialized)
    {
        return RES_NOTRDY;
    }

    s_lastStatus = SD_WriteBlocks(&s_card, buff, (uint32_t)sector, count);

    return (s_lastStatus == kSD_Ok) ? RES_OK : RES_ERROR;
}

DRESULT sdspi_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    DRESULT res = RES_OK;

    if (pdrv != SDSPIDISK)
    {
        return RES_PARERR;
    }
    if (!s_card.initialized)
    {
        return RES_NOTRDY;
    }

    switch (cmd)
    {
        case CTRL_SYNC:
            s_lastStatus = SD_Sync(&s_card);
            res          = (s_lastStatus == kSD_Ok) ? RES_OK : RES_ERROR;
            break;

        case GET_SECTOR_COUNT:
            if (buff == NULL)
            {
                res = RES_PARERR;
            }
            else
            {
                *(LBA_t *)buff = (LBA_t)s_card.sectorCount;
            }
            break;

        case GET_SECTOR_SIZE:
            if (buff == NULL)
            {
                res = RES_PARERR;
            }
            else
            {
                *(WORD *)buff = (WORD)SD_SPI_SECTOR_SIZE;
            }
            break;

        case GET_BLOCK_SIZE:
            if (buff == NULL)
            {
                res = RES_PARERR;
            }
            else
            {
                *(DWORD *)buff = (DWORD)s_card.eraseBlockSectors;
            }
            break;

        default:
            res = RES_PARERR;
            break;
    }

    return res;
}
