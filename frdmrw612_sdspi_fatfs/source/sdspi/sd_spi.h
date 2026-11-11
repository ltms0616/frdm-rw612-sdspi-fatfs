/*
 * Minimal SD card driver, SPI mode (SDSC / SDHC / SDXC).
 * Hardware access goes through sd_spi_port.h.
 */
#ifndef SD_SPI_H_
#define SD_SPI_H_

#include <stdbool.h>
#include <stdint.h>

/* SPI clock after initialisation. The port picks the highest rate that does not exceed this. */
#ifndef SD_SPI_DEFAULT_HZ
#define SD_SPI_DEFAULT_HZ 20000000U
#endif

/* Print the failing command and decoded R1 / token on errors. */
#ifndef SD_SPI_LOG_ERRORS
#define SD_SPI_LOG_ERRORS 1
#endif

/* 1: collect timing of the read path (token wait vs. data phase) in g_sdProfile. Diagnostic only. */
#ifndef SD_SPI_PROFILE
#define SD_SPI_PROFILE 0
#endif

/* Timeouts in milliseconds. */
#define SD_SPI_ACMD41_TIMEOUT_MS 1000U
#define SD_SPI_READ_TIMEOUT_MS   100U
#define SD_SPI_WRITE_TIMEOUT_MS  1000U
#define SD_SPI_READY_TIMEOUT_MS  500U

#define SD_SPI_SECTOR_SIZE 512U

typedef enum
{
    kSD_Ok = 0,
    kSD_ErrParam,          /* bad argument */
    kSD_ErrNotInit,        /* card not initialised */
    kSD_ErrSpi,            /* SPI transfer failed */
    kSD_ErrNoResponse,     /* no R1 (MISO stayed 0xFF) */
    kSD_ErrCmd0,           /* CMD0 did not return idle (0x01) */
    kSD_ErrCmd8Echo,       /* CMD8 check pattern / voltage mismatch */
    kSD_ErrUnsupported,    /* not an SD card (MMC) or unusable voltage range */
    kSD_ErrAcmd41Timeout,  /* card never left idle state */
    kSD_ErrR1,             /* command rejected, see lastR1 */
    kSD_ErrDataToken,      /* timeout or error token while waiting for a data block */
    kSD_ErrWriteRejected,  /* data response token was not "accepted" */
    kSD_ErrWriteStatus,    /* CMD13 reported an error after programming */
    kSD_ErrBusyTimeout,    /* card held MISO low for too long */
} sd_status_t;

typedef enum
{
    kSD_TypeUnknown = 0,
    kSD_TypeSDSC,
    kSD_TypeSDHC,
    kSD_TypeSDXC,
} sd_type_t;

typedef struct
{
    bool initialized;
    bool blockAddressing;  /* true: commands take sector numbers (SDHC/SDXC); false: byte offsets (SDSC) */
    bool version2;         /* answered CMD8 (physical layer 2.00 or later) */
    sd_type_t type;
    uint32_t ocr;
    uint8_t cid[16];
    uint8_t csd[16];
    uint32_t sectorCount;      /* 512-byte sectors */
    uint32_t eraseBlockSectors; /* erase block size in sectors, for FatFs GET_BLOCK_SIZE */
    uint32_t clockHz;          /* SPI clock in use after init */
    uint8_t lastCmd;           /* command index of the last failure */
    uint8_t lastR1;            /* R1 (or token) of the last failure */
} sd_card_t;

#if SD_SPI_PROFILE
/* All durations are CPU cycles (SystemCoreClock). */
typedef struct
{
    uint32_t readCalls;    /* SD_ReadBlocks() calls */
    uint32_t readBlocks;   /* 512-byte data blocks received */
    uint32_t tokenPolls;   /* bytes clocked while waiting for the start token */
    uint64_t readTotal;    /* whole SD_ReadBlocks() */
    uint64_t readCommand;  /* CMD17/18 until R1 */
    uint64_t readToken;    /* waiting for the 0xFE start token */
    uint64_t readData;     /* 512-byte payload transfer */
    uint64_t readCrc;      /* 2 CRC bytes */
    uint64_t readStop;     /* CMD12 + busy wait */
} sd_profile_t;

extern sd_profile_t g_sdProfile;
#endif

#if defined(__cplusplus)
extern "C" {
#endif

/* Bring up the SPI port and initialise the card. maxHz = 0 selects SD_SPI_DEFAULT_HZ. */
sd_status_t SD_Init(sd_card_t *card, uint32_t maxHz);

sd_status_t SD_ReadBlocks(sd_card_t *card, uint8_t *buffer, uint32_t sector, uint32_t count);
sd_status_t SD_WriteBlocks(sd_card_t *card, const uint8_t *buffer, uint32_t sector, uint32_t count);

/* Wait until the card has finished any internal programming. */
sd_status_t SD_Sync(sd_card_t *card);

/* Forget the card (after removal). The next SD_Init() starts from scratch. */
void SD_Deinit(sd_card_t *card);

/*
 * Card presence. Uses the card-detect switch when the port has one; otherwise an initialised card is
 * probed with CMD13. Without a switch an uninitialised card always reports false: try SD_Init().
 */
bool SD_IsPresent(sd_card_t *card);

/* Silence / restore the driver's error prints, e.g. while polling an empty slot with SD_Init(). */
void SD_SetLogEnabled(bool enable);

const char *SD_StatusString(sd_status_t status);
const char *SD_TypeString(sd_type_t type);

#if defined(__cplusplus)
}
#endif

#endif /* SD_SPI_H_ */
