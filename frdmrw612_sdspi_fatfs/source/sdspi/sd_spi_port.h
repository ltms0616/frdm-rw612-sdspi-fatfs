/*
 * SD card SPI-mode host port for FRDM-RW612 (Flexcomm1 SPI, CS on GPIO6).
 *
 * Wiring (all 3.3 V):
 *   SCK  GPIO7  J2 pin 12 -> microSD CLK
 *   MOSI GPIO9  J1 pin 2  -> microSD CMD  (DI)
 *   MISO GPIO8  J1 pin 4  -> microSD DAT0 (DO)
 *   CS   GPIO6  J2 pin 6  -> microSD CD/DAT3
 */
#ifndef SD_SPI_PORT_H_
#define SD_SPI_PORT_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define SD_SPI_PORT_BASE      SPI1
#define SD_SPI_PORT_FC_INDEX  1U
#define SD_SPI_PORT_CLK_ATTACH kFFRO_to_FLEXCOMM1
#define SD_SPI_PORT_CS_PORT   0U
#define SD_SPI_PORT_CS_PIN    6U

#define SD_SPI_INIT_HZ 400000U

/* 1: own FIFO polling loop (fast even at -O0); 0: SDK SPI_MasterTransferBlocking(). */
#ifndef SD_SPI_PORT_FAST_XFER
#define SD_SPI_PORT_FAST_XFER 1
#endif
#define SD_SPI_PORT_FIFO_DEPTH 8U

/*
 * 1: build DMA support (fsl_spi_dma) for block payloads. It can still be switched at run time with
 * SD_SPI_PortSetDmaEnabled(). Needs the driver.flexcomm_spi_dma and driver.lpc_dma components.
 */
#ifndef SD_SPI_PORT_DMA
#define SD_SPI_PORT_DMA 0
#endif
#define SD_SPI_PORT_DMA_BASE       DMA0
#define SD_SPI_PORT_DMA_IRQ        DMA0_IRQn
#define SD_SPI_PORT_DMA_RX_CH      2U    /* Flexcomm1 RX request */
#define SD_SPI_PORT_DMA_TX_CH      3U    /* Flexcomm1 TX request */
#define SD_SPI_PORT_DMA_MIN_LEN    64U   /* shorter transfers stay on the polling loop */
#define SD_SPI_PORT_DMA_MAX_LEN    1024U /* one DMA descriptor */
#define SD_SPI_PORT_DMA_TIMEOUT_MS 1000U

/*
 * 1: FreeRTOS build. Bus access is serialised with a recursive mutex, delays use vTaskDelay() and a
 * DMA transfer blocks the calling task on a semaphore instead of spinning.
 */
#ifndef SD_SPI_PORT_FREERTOS
#define SD_SPI_PORT_FREERTOS 0
#endif

/*
 * 1: a card-detect switch is wired to a GPIO. Default wiring assumption: switch to GND when a card
 * is inserted, internal pull-up enabled. With 0 the driver probes the card with CMD13 instead.
 */
#ifndef SD_SPI_PORT_CD_ENABLE
#define SD_SPI_PORT_CD_ENABLE 0
#endif
#define SD_SPI_PORT_CD_IOMUX        IO_MUX_GPIO11 /* J1 pin 6 (ARD_D2) */
#define SD_SPI_PORT_CD_PORT         0U
#define SD_SPI_PORT_CD_PIN          11U
#define SD_SPI_PORT_CD_ACTIVE_LEVEL 0U

#if defined(__cplusplus)
extern "C" {
#endif

/* Attach the Flexcomm clock, drive CS high and bring SPI up at SD_SPI_INIT_HZ (mode 0, 8-bit, MSB first). */
void SD_SPI_PortInit(void);

/* Set the SPI clock to the highest rate that does not exceed maxHz. Returns the actual rate in Hz. */
uint32_t SD_SPI_PortSetFrequency(uint32_t maxHz);

/* Flexcomm function clock in Hz. */
uint32_t SD_SPI_PortGetSourceClock(void);

/* true = CS low (selected), false = CS high. */
void SD_SPI_PortSelect(bool select);

/*
 * Full-duplex transfer of len bytes. tx == NULL sends 0xFF, rx == NULL discards received data.
 * Returns true on success.
 */
bool SD_SPI_PortExchange(const uint8_t *tx, uint8_t *rx, size_t len);

/* Exchange one byte (always polling). */
uint8_t SD_SPI_PortXferByte(uint8_t tx);

/* Delay: vTaskDelay() once the FreeRTOS scheduler runs, busy-wait otherwise. */
void SD_SPI_PortDelayMs(uint32_t ms);

/* Run-time DMA switch; no effect when built with SD_SPI_PORT_DMA == 0. */
void SD_SPI_PortSetDmaEnabled(bool enable);
bool SD_SPI_PortGetDmaEnabled(void);

/* Bus lock around a complete card transaction. No-op in bare-metal builds. Recursive. */
void SD_SPI_PortLock(void);
void SD_SPI_PortUnlock(void);

/* Card-detect switch: 1 = card present, 0 = no card, -1 = no switch configured. */
int SD_SPI_PortCardDetect(void);

/*
 * Time base on the CPU cycle counter (DWT CYCCNT), no interrupt needed.
 * A single interval must stay below 2^32 cycles (about 16 s at 260 MHz).
 */
uint32_t SD_SPI_PortTimeStart(void);
bool SD_SPI_PortTimeExpired(uint32_t start, uint32_t ms);
uint32_t SD_SPI_PortElapsedUs(uint32_t start);

#if defined(__cplusplus)
}
#endif

#endif /* SD_SPI_PORT_H_ */
