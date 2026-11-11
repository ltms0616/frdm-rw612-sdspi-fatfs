/*
 * Minimal SD card driver, SPI mode (SDSC / SDHC / SDXC).
 */
#include "sd_spi.h"

#include <stddef.h>
#include <string.h>

#include "sd_spi_port.h"

#if SD_SPI_LOG_ERRORS
#include "fsl_debug_console.h"
static bool s_logEnabled = true;
#define SD_LOG(...)                    \
    do                                 \
    {                                  \
        if (s_logEnabled)              \
        {                              \
            (void)PRINTF(__VA_ARGS__); \
        }                              \
    } while (false)
#else
#define SD_LOG(...)
#endif

#if SD_SPI_PROFILE
sd_profile_t g_sdProfile;
#define PROF_START(var)      uint32_t var = SD_SPI_PortTimeStart()
#define PROF_ADD(field, var) g_sdProfile.field += (uint32_t)(SD_SPI_PortTimeStart() - (var))
#define PROF_INC(field, n)   g_sdProfile.field += (n)
#else
#define PROF_START(var)
#define PROF_ADD(field, var)
#define PROF_INC(field, n)
#endif

/* Commands */
#define CMD0   0U  /* GO_IDLE_STATE */
#define CMD8   8U  /* SEND_IF_COND */
#define CMD9   9U  /* SEND_CSD */
#define CMD10  10U /* SEND_CID */
#define CMD12  12U /* STOP_TRANSMISSION */
#define CMD13  13U /* SEND_STATUS */
#define CMD16  16U /* SET_BLOCKLEN */
#define CMD17  17U /* READ_SINGLE_BLOCK */
#define CMD18  18U /* READ_MULTIPLE_BLOCK */
#define CMD24  24U /* WRITE_BLOCK */
#define CMD25  25U /* WRITE_MULTIPLE_BLOCK */
#define CMD55  55U /* APP_CMD */
#define CMD58  58U /* READ_OCR */
#define ACMD23 23U /* SET_WR_BLK_ERASE_COUNT */
#define ACMD41 41U /* SD_SEND_OP_COND */

/* R1 bits */
#define R1_IDLE          0x01U
#define R1_ERASE_RESET   0x02U
#define R1_ILLEGAL_CMD   0x04U
#define R1_CRC_ERROR     0x08U
#define R1_ERASE_SEQ     0x10U
#define R1_ADDRESS_ERROR 0x20U
#define R1_PARAM_ERROR   0x40U
#define R1_NOT_R1        0x80U

/* Tokens */
#define TOKEN_START_BLOCK       0xFEU /* CMD17/18/24, CMD9/10 */
#define TOKEN_START_MULTI_WRITE 0xFCU /* CMD25 data block */
#define TOKEN_STOP_MULTI_WRITE  0xFDU /* CMD25 end */
#define DATA_RESP_MASK          0x1FU
#define DATA_RESP_ACCEPTED      0x05U

#define OCR_CCS     0x40000000UL /* card capacity status: 1 = SDHC/SDXC */
#define OCR_3V2_3V4 0x00300000UL /* 3.2-3.3 V and 3.3-3.4 V windows */
#define ACMD41_HCS  0x40000000UL

#define CMD0_ATTEMPTS  10U
#define R1_POLL_BYTES  10U /* NCR is at most 8 bytes */

/* ---------------------------------------------------------------------------------------------- */

static uint8_t sd_crc7(const uint8_t *data, uint32_t len)
{
    uint8_t crc = 0U;
    uint32_t i;
    uint32_t bit;

    for (i = 0U; i < len; i++)
    {
        uint8_t d = data[i];
        for (bit = 0U; bit < 8U; bit++)
        {
            crc = (uint8_t)(crc << 1);
            if (((d ^ crc) & 0x80U) != 0U)
            {
                crc ^= 0x09U;
            }
            d = (uint8_t)(d << 1);
        }
    }
    return (uint8_t)((crc << 1) | 1U);
}

#if SD_SPI_LOG_ERRORS
static void sd_log_r1(const char *what, uint8_t cmd, uint8_t r1)
{
    SD_LOG("[sd] %s%u: R1=0x%02X", what, cmd, r1);
    if (r1 == 0xFFU)
    {
        SD_LOG(" (no response)");
    }
    else if ((r1 & R1_NOT_R1) != 0U)
    {
        SD_LOG(" (not a valid R1)");
    }
    else
    {
        if ((r1 & R1_IDLE) != 0U)
        {
            SD_LOG(" idle");
        }
        if ((r1 & R1_ERASE_RESET) != 0U)
        {
            SD_LOG(" erase-reset");
        }
        if ((r1 & R1_ILLEGAL_CMD) != 0U)
        {
            SD_LOG(" illegal-command");
        }
        if ((r1 & R1_CRC_ERROR) != 0U)
        {
            SD_LOG(" crc-error");
        }
        if ((r1 & R1_ERASE_SEQ) != 0U)
        {
            SD_LOG(" erase-sequence-error");
        }
        if ((r1 & R1_ADDRESS_ERROR) != 0U)
        {
            SD_LOG(" address-error");
        }
        if ((r1 & R1_PARAM_ERROR) != 0U)
        {
            SD_LOG(" parameter-error");
        }
    }
    SD_LOG("\r\n");
}
#else
#define sd_log_r1(what, cmd, r1)
#endif

static void sd_deselect(void)
{
    SD_SPI_PortSelect(false);
    /* The card releases MISO one clock after CS goes high. */
    (void)SD_SPI_PortXferByte(0xFFU);
}

/* Wait until the card stops driving MISO low. */
static bool sd_wait_ready(uint32_t timeoutMs)
{
    uint32_t start = SD_SPI_PortTimeStart();

    do
    {
        if (SD_SPI_PortXferByte(0xFFU) == 0xFFU)
        {
            return true;
        }
    } while (!SD_SPI_PortTimeExpired(start, timeoutMs));

    return false;
}

/*
 * Send a command with CS already low and return R1 (0xFF if the card did not answer).
 * Any further response bytes are left for the caller.
 */
static uint8_t sd_send_cmd(uint8_t cmd, uint32_t arg)
{
    uint8_t frame[6];
    uint8_t r1 = 0xFFU;
    uint32_t i;

    /* CMD0 must work on a card in an unknown state; CMD12 is sent while the card is transmitting. */
    if ((cmd != CMD0) && (cmd != CMD12))
    {
        if (!sd_wait_ready(SD_SPI_READY_TIMEOUT_MS))
        {
            return 0xFFU;
        }
    }

    frame[0] = (uint8_t)(0x40U | cmd);
    frame[1] = (uint8_t)(arg >> 24);
    frame[2] = (uint8_t)(arg >> 16);
    frame[3] = (uint8_t)(arg >> 8);
    frame[4] = (uint8_t)arg;
    frame[5] = sd_crc7(frame, 5U); /* CMD0 -> 0x95, CMD8(0x1AA) -> 0x87 */

    if (!SD_SPI_PortExchange(frame, NULL, sizeof(frame)))
    {
        return 0xFFU;
    }

    if (cmd == CMD12)
    {
        (void)SD_SPI_PortXferByte(0xFFU); /* stuff byte */
    }

    for (i = 0U; i < R1_POLL_BYTES; i++)
    {
        r1 = SD_SPI_PortXferByte(0xFFU);
        if ((r1 & R1_NOT_R1) == 0U)
        {
            break;
        }
    }

    return r1;
}

/* Select, send one command, optionally read extra response bytes, deselect. */
static uint8_t sd_command(uint8_t cmd, uint32_t arg, uint8_t *extra, uint32_t extraLen)
{
    uint8_t r1;

    SD_SPI_PortSelect(true);
    r1 = sd_send_cmd(cmd, arg);
    if ((extra != NULL) && ((r1 & R1_NOT_R1) == 0U))
    {
        (void)SD_SPI_PortExchange(NULL, extra, extraLen);
    }
    sd_deselect();

    return r1;
}

static sd_status_t sd_fail_r1(sd_card_t *card, const char *what, uint8_t cmd, uint8_t r1)
{
    (void)what;
    card->lastCmd = cmd;
    card->lastR1  = r1;
    sd_log_r1(what, cmd, r1);
    return ((r1 & R1_NOT_R1) != 0U) ? kSD_ErrNoResponse : kSD_ErrR1;
}

/* Receive one data block (start token, payload, CRC16). CS must be low. */
static sd_status_t sd_read_data(sd_card_t *card, uint8_t *buffer, uint32_t len)
{
    uint32_t start = SD_SPI_PortTimeStart();
    uint8_t token;

    do
    {
        token = SD_SPI_PortXferByte(0xFFU);
        PROF_INC(tokenPolls, 1U);
    } while ((token == 0xFFU) && !SD_SPI_PortTimeExpired(start, SD_SPI_READ_TIMEOUT_MS));
    PROF_ADD(readToken, start);

    if (token != TOKEN_START_BLOCK)
    {
        card->lastR1 = token;
        if (token == 0xFFU)
        {
            SD_LOG("[sd] data token timeout\r\n");
        }
        else
        {
            /* Data error token: 0000xxxx (bit0 error, bit1 CC error, bit2 ECC failed, bit3 out of range). */
            SD_LOG("[sd] data error token 0x%02X\r\n", token);
        }
        return kSD_ErrDataToken;
    }

    {
        PROF_START(tData);
        if (!SD_SPI_PortExchange(NULL, buffer, len))
        {
            return kSD_ErrSpi;
        }
        PROF_ADD(readData, tData);
    }
    {
        PROF_START(tCrc);
        (void)SD_SPI_PortXferByte(0xFFU); /* CRC16, not checked (CRC is off in SPI mode) */
        (void)SD_SPI_PortXferByte(0xFFU);
        PROF_ADD(readCrc, tCrc);
    }
    PROF_INC(readBlocks, 1U);

    return kSD_Ok;
}

/* Send one data block and wait for programming to finish. CS must be low. */
static sd_status_t sd_write_data(sd_card_t *card, const uint8_t *buffer, uint8_t token)
{
    uint8_t resp;

    (void)SD_SPI_PortXferByte(0xFFU); /* at least one byte gap before the token */
    (void)SD_SPI_PortXferByte(token);
    if (!SD_SPI_PortExchange(buffer, NULL, SD_SPI_SECTOR_SIZE))
    {
        return kSD_ErrSpi;
    }
    (void)SD_SPI_PortXferByte(0xFFU); /* dummy CRC16 */
    (void)SD_SPI_PortXferByte(0xFFU);

    resp = SD_SPI_PortXferByte(0xFFU);
    if ((resp & DATA_RESP_MASK) != DATA_RESP_ACCEPTED)
    {
        card->lastR1 = resp;
        /* xxx0_101_1 = CRC error, xxx0_110_1 = write error */
        SD_LOG("[sd] data response 0x%02X (rejected)\r\n", resp);
        return kSD_ErrWriteRejected;
    }

    if (!sd_wait_ready(SD_SPI_WRITE_TIMEOUT_MS))
    {
        SD_LOG("[sd] write busy timeout\r\n");
        return kSD_ErrBusyTimeout;
    }

    return kSD_Ok;
}

/* Read a 16-byte register (CSD or CID). */
static sd_status_t sd_read_register(sd_card_t *card, uint8_t cmd, uint8_t *out)
{
    sd_status_t status;
    uint8_t r1;

    SD_SPI_PortSelect(true);
    r1 = sd_send_cmd(cmd, 0U);
    if (r1 != 0U)
    {
        sd_deselect();
        return sd_fail_r1(card, "CMD", cmd, r1);
    }
    status = sd_read_data(card, out, 16U);
    sd_deselect();

    return status;
}

static void sd_parse_csd(sd_card_t *card)
{
    const uint8_t *csd = card->csd;
    uint8_t structure  = (uint8_t)(csd[0] >> 6);

    if (structure == 1U)
    {
        /* CSD version 2.0: C_SIZE is in units of 512 KiB. */
        uint32_t cSize = (((uint32_t)csd[7] & 0x3FU) << 16) | ((uint32_t)csd[8] << 8) | csd[9];

        card->sectorCount = (cSize + 1U) * 1024U;
        /* The real allocation unit is only in the SD status register; 4 MiB is the usual value. */
        card->eraseBlockSectors = 8192U;
    }
    else
    {
        /* CSD version 1.0 */
        uint32_t readBlLen  = (uint32_t)csd[5] & 0x0FU;
        uint32_t cSize      = (((uint32_t)csd[6] & 0x03U) << 10) | ((uint32_t)csd[7] << 2) | ((uint32_t)csd[8] >> 6);
        uint32_t cSizeMult  = (((uint32_t)csd[9] & 0x03U) << 1) | ((uint32_t)csd[10] >> 7);
        uint32_t sectorSize = ((((uint32_t)csd[10] & 0x3FU) << 1) | ((uint32_t)csd[11] >> 7)) + 1U;
        uint32_t writeBlLen = (((uint32_t)csd[12] & 0x03U) << 2) | ((uint32_t)csd[13] >> 6);
        uint32_t blocks     = (cSize + 1U) << (cSizeMult + 2U);

        card->sectorCount       = (readBlLen >= 9U) ? (blocks << (readBlLen - 9U)) : (blocks >> (9U - readBlLen));
        card->eraseBlockSectors = (writeBlLen >= 9U) ? (sectorSize << (writeBlLen - 9U)) : 1U;
    }

    if (!card->blockAddressing)
    {
        card->type = kSD_TypeSDSC;
    }
    else
    {
        /* SDXC starts above 32 GiB = 67108864 sectors. */
        card->type = (card->sectorCount > 67108864UL) ? kSD_TypeSDXC : kSD_TypeSDHC;
    }
}

/* ---------------------------------------------------------------------------------------------- */

static sd_status_t sd_init_locked(sd_card_t *card, uint32_t maxHz)
{
    sd_status_t status;
    uint8_t r1 = 0xFFU;
    uint8_t resp[4];
    uint32_t start;
    uint32_t i;

    if (card == NULL)
    {
        return kSD_ErrParam;
    }
    (void)memset(card, 0, sizeof(*card));

    SD_SPI_PortInit(); /* 400 kHz, CS high */

    /* CMD0: enter SPI mode. Each attempt is preceded by >= 74 clocks with CS high. */
    for (i = 0U; i < CMD0_ATTEMPTS; i++)
    {
        SD_SPI_PortSelect(false);
        (void)SD_SPI_PortExchange(NULL, NULL, 10U);

        r1 = sd_command(CMD0, 0U, NULL, 0U);
        if (r1 == R1_IDLE)
        {
            break;
        }
        SD_SPI_PortDelayMs(10U);
    }
    if (r1 != R1_IDLE)
    {
        (void)sd_fail_r1(card, "CMD", CMD0, r1);
        return kSD_ErrCmd0;
    }

    /* CMD8: 2.7-3.6 V, check pattern 0xAA. Version 1 cards answer "illegal command". */
    r1 = sd_command(CMD8, 0x000001AAUL, resp, sizeof(resp));
    if ((r1 & R1_NOT_R1) != 0U)
    {
        return sd_fail_r1(card, "CMD", CMD8, r1);
    }
    if ((r1 & R1_ILLEGAL_CMD) != 0U)
    {
        card->version2 = false;
    }
    else
    {
        if (((resp[2] & 0x0FU) != 0x01U) || (resp[3] != 0xAAU))
        {
            SD_LOG("[sd] CMD8 echo mismatch: %02X %02X %02X %02X\r\n", resp[0], resp[1], resp[2], resp[3]);
            card->lastCmd = CMD8;
            card->lastR1  = r1;
            return kSD_ErrCmd8Echo;
        }
        card->version2 = true;
    }

    /* ACMD41 until the card leaves idle state. */
    start = SD_SPI_PortTimeStart();
    for (;;)
    {
        r1 = sd_command(CMD55, 0U, NULL, 0U);
        if ((r1 & (uint8_t)~R1_IDLE) != 0U)
        {
            (void)sd_fail_r1(card, "CMD", CMD55, r1);
            return ((r1 & R1_ILLEGAL_CMD) != 0U) ? kSD_ErrUnsupported : kSD_ErrNoResponse;
        }

        r1 = sd_command(ACMD41, card->version2 ? ACMD41_HCS : 0U, NULL, 0U);
        if (r1 == 0U)
        {
            break;
        }
        if (r1 != R1_IDLE)
        {
            (void)sd_fail_r1(card, "ACMD", ACMD41, r1);
            /* An MMC card rejects ACMD41. */
            return ((r1 & R1_ILLEGAL_CMD) != 0U) ? kSD_ErrUnsupported : kSD_ErrR1;
        }
        if (SD_SPI_PortTimeExpired(start, SD_SPI_ACMD41_TIMEOUT_MS))
        {
            SD_LOG("[sd] ACMD41 timeout, card still idle\r\n");
            card->lastCmd = ACMD41;
            card->lastR1  = r1;
            return kSD_ErrAcmd41Timeout;
        }
        SD_SPI_PortDelayMs(1U);
    }

    /* CMD58: OCR. CCS selects sector addressing. */
    r1 = sd_command(CMD58, 0U, resp, sizeof(resp));
    if (r1 != 0U)
    {
        return sd_fail_r1(card, "CMD", CMD58, r1);
    }
    card->ocr = ((uint32_t)resp[0] << 24) | ((uint32_t)resp[1] << 16) | ((uint32_t)resp[2] << 8) | resp[3];
    if ((card->ocr & OCR_3V2_3V4) == 0U)
    {
        SD_LOG("[sd] OCR 0x%08X: 3.3 V not supported\r\n", card->ocr);
        return kSD_ErrUnsupported;
    }
    card->blockAddressing = card->version2 && ((card->ocr & OCR_CCS) != 0U);

    if (!card->blockAddressing)
    {
        r1 = sd_command(CMD16, SD_SPI_SECTOR_SIZE, NULL, 0U);
        if (r1 != 0U)
        {
            return sd_fail_r1(card, "CMD", CMD16, r1);
        }
    }

    status = sd_read_register(card, CMD9, card->csd);
    if (status != kSD_Ok)
    {
        return status;
    }
    status = sd_read_register(card, CMD10, card->cid);
    if (status != kSD_Ok)
    {
        return status;
    }
    sd_parse_csd(card);

    card->clockHz     = SD_SPI_PortSetFrequency((maxHz != 0U) ? maxHz : SD_SPI_DEFAULT_HZ);
    card->initialized = true;

    return kSD_Ok;
}

static sd_status_t sd_read_blocks_locked(sd_card_t *card, uint8_t *buffer, uint32_t sector, uint32_t count)
{
    sd_status_t status = kSD_Ok;
    uint32_t addr;
    uint8_t cmd;
    uint8_t r1;

    if ((card == NULL) || (buffer == NULL) || (count == 0U))
    {
        return kSD_ErrParam;
    }
    if (!card->initialized)
    {
        return kSD_ErrNotInit;
    }
    if ((sector >= card->sectorCount) || (count > (card->sectorCount - sector)))
    {
        return kSD_ErrParam;
    }

    addr = card->blockAddressing ? sector : (sector * SD_SPI_SECTOR_SIZE);
    cmd  = (count == 1U) ? CMD17 : CMD18;

    PROF_START(tTotal);
    PROF_INC(readCalls, 1U);
    SD_SPI_PortSelect(true);
    r1 = sd_send_cmd(cmd, addr);
    PROF_ADD(readCommand, tTotal);
    if (r1 != 0U)
    {
        sd_deselect();
        return sd_fail_r1(card, "CMD", cmd, r1);
    }

    while (count != 0U)
    {
        status = sd_read_data(card, buffer, SD_SPI_SECTOR_SIZE);
        if (status != kSD_Ok)
        {
            break;
        }
        buffer += SD_SPI_SECTOR_SIZE;
        count--;
    }

    PROF_START(tStop);
    if (cmd == CMD18)
    {
        /* Stop the stream even after an error so the card returns to the transfer state. */
        (void)sd_send_cmd(CMD12, 0U);
        if (!sd_wait_ready(SD_SPI_READY_TIMEOUT_MS) && (status == kSD_Ok))
        {
            status = kSD_ErrBusyTimeout;
        }
    }
    sd_deselect();
    PROF_ADD(readStop, tStop);
    PROF_ADD(readTotal, tTotal);

    return status;
}

static sd_status_t sd_write_blocks_locked(sd_card_t *card, const uint8_t *buffer, uint32_t sector, uint32_t count)
{
    sd_status_t status = kSD_Ok;
    uint32_t addr;
    uint8_t r1;
    uint8_t r2[1];

    if ((card == NULL) || (buffer == NULL) || (count == 0U))
    {
        return kSD_ErrParam;
    }
    if (!card->initialized)
    {
        return kSD_ErrNotInit;
    }
    if ((sector >= card->sectorCount) || (count > (card->sectorCount - sector)))
    {
        return kSD_ErrParam;
    }

    addr = card->blockAddressing ? sector : (sector * SD_SPI_SECTOR_SIZE);

    if (count == 1U)
    {
        SD_SPI_PortSelect(true);
        r1 = sd_send_cmd(CMD24, addr);
        if (r1 != 0U)
        {
            sd_deselect();
            return sd_fail_r1(card, "CMD", CMD24, r1);
        }
        status = sd_write_data(card, buffer, TOKEN_START_BLOCK);
        sd_deselect();
    }
    else
    {
        /* Pre-erase hint; failure is not fatal. */
        if (sd_command(CMD55, 0U, NULL, 0U) == 0U)
        {
            (void)sd_command(ACMD23, count & 0x007FFFFFUL, NULL, 0U);
        }

        SD_SPI_PortSelect(true);
        r1 = sd_send_cmd(CMD25, addr);
        if (r1 != 0U)
        {
            sd_deselect();
            return sd_fail_r1(card, "CMD", CMD25, r1);
        }

        while (count != 0U)
        {
            status = sd_write_data(card, buffer, TOKEN_START_MULTI_WRITE);
            if (status != kSD_Ok)
            {
                break;
            }
            buffer += SD_SPI_SECTOR_SIZE;
            count--;
        }

        if (status == kSD_Ok)
        {
            (void)SD_SPI_PortXferByte(TOKEN_STOP_MULTI_WRITE);
            (void)SD_SPI_PortXferByte(0xFFU); /* busy becomes valid one byte later */
            if (!sd_wait_ready(SD_SPI_WRITE_TIMEOUT_MS))
            {
                SD_LOG("[sd] stop-tran busy timeout\r\n");
                status = kSD_ErrBusyTimeout;
            }
        }
        else
        {
            /* A rejected block ends the transfer; CMD12 brings the card back to the transfer state. */
            (void)sd_send_cmd(CMD12, 0U);
            (void)sd_wait_ready(SD_SPI_WRITE_TIMEOUT_MS);
        }
        sd_deselect();
    }

    if (status == kSD_Ok)
    {
        /* CMD13 (R2): any set bit means the programming failed. */
        r1 = sd_command(CMD13, 0U, r2, sizeof(r2));
        if ((r1 != 0U) || (r2[0] != 0U))
        {
            SD_LOG("[sd] CMD13 after write: R2=0x%02X%02X\r\n", r1, r2[0]);
            card->lastCmd = CMD13;
            card->lastR1  = r1;
            status        = kSD_ErrWriteStatus;
        }
    }

    return status;
}

static sd_status_t sd_sync_locked(sd_card_t *card)
{
    bool ready;

    if (card == NULL)
    {
        return kSD_ErrParam;
    }
    if (!card->initialized)
    {
        return kSD_ErrNotInit;
    }

    SD_SPI_PortSelect(true);
    ready = sd_wait_ready(SD_SPI_WRITE_TIMEOUT_MS);
    sd_deselect();

    return ready ? kSD_Ok : kSD_ErrBusyTimeout;
}

/* Public API: every call is one bus transaction under the port lock. */
sd_status_t SD_Init(sd_card_t *card, uint32_t maxHz)
{
    sd_status_t status;

    SD_SPI_PortLock();
    status = sd_init_locked(card, maxHz);
    SD_SPI_PortUnlock();
    return status;
}

sd_status_t SD_ReadBlocks(sd_card_t *card, uint8_t *buffer, uint32_t sector, uint32_t count)
{
    sd_status_t status;

    SD_SPI_PortLock();
    status = sd_read_blocks_locked(card, buffer, sector, count);
    SD_SPI_PortUnlock();
    return status;
}

sd_status_t SD_WriteBlocks(sd_card_t *card, const uint8_t *buffer, uint32_t sector, uint32_t count)
{
    sd_status_t status;

    SD_SPI_PortLock();
    status = sd_write_blocks_locked(card, buffer, sector, count);
    SD_SPI_PortUnlock();
    return status;
}

sd_status_t SD_Sync(sd_card_t *card)
{
    sd_status_t status;

    SD_SPI_PortLock();
    status = sd_sync_locked(card);
    SD_SPI_PortUnlock();
    return status;
}

void SD_Deinit(sd_card_t *card)
{
    if (card != NULL)
    {
        SD_SPI_PortLock();
        card->initialized = false;
        SD_SPI_PortUnlock();
    }
}

bool SD_IsPresent(sd_card_t *card)
{
    int cd = SD_SPI_PortCardDetect();
    uint8_t r1;
    uint8_t r2[1] = {0xFFU};

    if (cd >= 0)
    {
        return cd != 0;
    }
    if ((card == NULL) || !card->initialized)
    {
        return false;
    }

    /*
     * No switch: ask for the status. An initialised card answers R1 = 0x00. No card leaves MISO at
     * 0xFF; a card that was swapped is back in idle state (or silent) and must be initialised again.
     */
    SD_SPI_PortLock();
    r1 = sd_command(CMD13, 0U, r2, sizeof(r2));
    SD_SPI_PortUnlock();

    return r1 == 0U;
}

void SD_SetLogEnabled(bool enable)
{
#if SD_SPI_LOG_ERRORS
    s_logEnabled = enable;
#else
    (void)enable;
#endif
}

const char *SD_StatusString(sd_status_t status)
{
    switch (status)
    {
        case kSD_Ok:
            return "OK";
        case kSD_ErrParam:
            return "invalid parameter";
        case kSD_ErrNotInit:
            return "card not initialised";
        case kSD_ErrSpi:
            return "SPI transfer failed";
        case kSD_ErrNoResponse:
            return "no response from card";
        case kSD_ErrCmd0:
            return "CMD0 failed (card did not enter idle state)";
        case kSD_ErrCmd8Echo:
            return "CMD8 echo mismatch";
        case kSD_ErrUnsupported:
            return "unsupported card";
        case kSD_ErrAcmd41Timeout:
            return "ACMD41 timeout";
        case kSD_ErrR1:
            return "command rejected (R1 error)";
        case kSD_ErrDataToken:
            return "data token timeout or error";
        case kSD_ErrWriteRejected:
            return "write data rejected";
        case kSD_ErrWriteStatus:
            return "write failed (CMD13 status)";
        case kSD_ErrBusyTimeout:
            return "busy timeout";
        default:
            return "unknown error";
    }
}

const char *SD_TypeString(sd_type_t type)
{
    switch (type)
    {
        case kSD_TypeSDSC:
            return "SDSC";
        case kSD_TypeSDHC:
            return "SDHC";
        case kSD_TypeSDXC:
            return "SDXC";
        default:
            return "unknown";
    }
}
