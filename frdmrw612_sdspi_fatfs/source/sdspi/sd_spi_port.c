/*
 * SD card SPI-mode host port for FRDM-RW612 (Flexcomm1 SPI, CS on GPIO6).
 */
#include "sd_spi_port.h"

#include <string.h>

#include "fsl_clock.h"
#include "fsl_common.h"
#include "fsl_gpio.h"
#include "fsl_spi.h"

#if SD_SPI_PORT_DMA
#include "fsl_dma.h"
#include "fsl_spi_dma.h"
#endif

#if SD_SPI_PORT_CD_ENABLE
#include "fsl_io_mux.h"
#endif

#if SD_SPI_PORT_FREERTOS
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#endif

/*******************************************************************************
 * Variables
 ******************************************************************************/
#if SD_SPI_PORT_DMA
static dma_handle_t s_dmaTx;
static dma_handle_t s_dmaRx;
static spi_dma_handle_t s_spiDma;
static bool s_dmaEnabled = true;
#if SD_SPI_PORT_FREERTOS
static SemaphoreHandle_t s_dmaDone;
#else
static volatile bool s_dmaDone;
#endif
#endif /* SD_SPI_PORT_DMA */

#if SD_SPI_PORT_FREERTOS
static SemaphoreHandle_t s_busMutex;
#endif

/*******************************************************************************
 * Code
 ******************************************************************************/
uint32_t SD_SPI_PortGetSourceClock(void)
{
    return CLOCK_GetFlexCommClkFreq(SD_SPI_PORT_FC_INDEX);
}

void SD_SPI_PortSelect(bool select)
{
    GPIO_PinWrite(GPIO, SD_SPI_PORT_CS_PORT, SD_SPI_PORT_CS_PIN, select ? 0U : 1U);
}

uint32_t SD_SPI_PortSetFrequency(uint32_t maxHz)
{
    uint32_t src = SD_SPI_PortGetSourceClock();
    uint32_t div;

    if (maxHz == 0U)
    {
        maxHz = 1U;
    }

    /* SPI_MasterSetBaud() rounds to nearest and may overshoot; round the divider up instead. */
    div = (src + maxHz - 1U) / maxHz;
    if (div == 0U)
    {
        div = 1U;
    }
    if (div > 0x10000U)
    {
        div = 0x10000U;
    }

    SD_SPI_PORT_BASE->DIV = (SD_SPI_PORT_BASE->DIV & ~SPI_DIV_DIVVAL_MASK) | SPI_DIV_DIVVAL(div - 1U);

    return src / div;
}

#if SD_SPI_PORT_DMA
static void sd_spi_dma_callback(SPI_Type *base, spi_dma_handle_t *handle, status_t status, void *userData)
{
    (void)base;
    (void)handle;
    (void)status;
    (void)userData;

#if SD_SPI_PORT_FREERTOS
    BaseType_t woken = pdFALSE;

    (void)xSemaphoreGiveFromISR(s_dmaDone, &woken);
    portYIELD_FROM_ISR(woken);
#else
    s_dmaDone = true;
#endif
}

void SD_SPI_PortSetDmaEnabled(bool enable)
{
    s_dmaEnabled = enable;
}

bool SD_SPI_PortGetDmaEnabled(void)
{
    return s_dmaEnabled;
}
#else
void SD_SPI_PortSetDmaEnabled(bool enable)
{
    (void)enable;
}

bool SD_SPI_PortGetDmaEnabled(void)
{
    return false;
}
#endif /* SD_SPI_PORT_DMA */

void SD_SPI_PortInit(void)
{
    spi_master_config_t config;
    gpio_pin_config_t csConfig = {kGPIO_DigitalOutput, 1U};

    MSDK_EnableCpuCycleCounter();

    /* CS is a plain GPIO, idle high. The pin itself is muxed in BOARD_InitPins(). */
    GPIO_PortInit(GPIO, SD_SPI_PORT_CS_PORT);
    GPIO_PinInit(GPIO, SD_SPI_PORT_CS_PORT, SD_SPI_PORT_CS_PIN, &csConfig);

#if SD_SPI_PORT_CD_ENABLE
    {
        gpio_pin_config_t cdConfig = {kGPIO_DigitalInput, 0U};

        IO_MUX_SetPinMux(SD_SPI_PORT_CD_IOMUX);
        IO_MUX_SetPinConfig(SD_SPI_PORT_CD_PIN, IO_MUX_PinConfigPullUp);
        GPIO_PortInit(GPIO, SD_SPI_PORT_CD_PORT);
        GPIO_PinInit(GPIO, SD_SPI_PORT_CD_PORT, SD_SPI_PORT_CD_PIN, &cdConfig);
    }
#endif

    CLOCK_AttachClk(SD_SPI_PORT_CLK_ATTACH);

    SPI_MasterGetDefaultConfig(&config); /* mode 0, 8-bit, MSB first */
    config.baudRate_Bps = SD_SPI_INIT_HZ;
    /* Hardware SSEL is not routed to any pin; the driver still needs a valid selection. */
    config.sselNum = kSPI_Ssel1;
    (void)SPI_MasterInit(SD_SPI_PORT_BASE, &config, SD_SPI_PortGetSourceClock());

    /* SPI_MasterInit() resets the dummy byte to 0x00; an SD card needs MOSI high while reading. */
    SPI_SetDummyData(SD_SPI_PORT_BASE, 0xFFU);

    (void)SD_SPI_PortSetFrequency(SD_SPI_INIT_HZ);

#if SD_SPI_PORT_DMA
#if SD_SPI_PORT_FREERTOS
    if (s_dmaDone == NULL)
    {
        s_dmaDone = xSemaphoreCreateBinary();
    }
#endif
    DMA_Init(SD_SPI_PORT_DMA_BASE);
    DMA_EnableChannel(SD_SPI_PORT_DMA_BASE, SD_SPI_PORT_DMA_RX_CH);
    DMA_EnableChannel(SD_SPI_PORT_DMA_BASE, SD_SPI_PORT_DMA_TX_CH);
    DMA_CreateHandle(&s_dmaRx, SD_SPI_PORT_DMA_BASE, SD_SPI_PORT_DMA_RX_CH);
    DMA_CreateHandle(&s_dmaTx, SD_SPI_PORT_DMA_BASE, SD_SPI_PORT_DMA_TX_CH);
    (void)SPI_MasterTransferCreateHandleDMA(SD_SPI_PORT_BASE, &s_spiDma, sd_spi_dma_callback, NULL, &s_dmaTx, &s_dmaRx);
#if SD_SPI_PORT_FREERTOS
    /* The callback uses FromISR APIs, so the IRQ must not be above the kernel's syscall priority. */
    NVIC_SetPriority(SD_SPI_PORT_DMA_IRQ, configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY + 1U);
#endif
#endif /* SD_SPI_PORT_DMA */
}

#if SD_SPI_PORT_FAST_XFER
/*
 * Polling transfer written directly against the Flexcomm SPI FIFO registers.
 * SPI_MasterTransferBlocking() needs about 125 CPU cycles per byte in an unoptimised build, which is
 * slower than the wire above roughly 16 MHz. This loop is always compiled with -O2 so that debug
 * builds are not limited by the CPU.
 */
__attribute__((optimize("O2"))) static bool sd_spi_exchange_polling(const uint8_t *tx, uint8_t *rx, size_t len)
{
    SPI_Type *base = SD_SPI_PORT_BASE;
    /* 8-bit frames; same slave-select bits as the SDK driver (SSEL1 selected, not routed to a pin). */
    const uint32_t ctrl = SPI_FIFOWR_LEN(kSPI_Data8Bits) | (SPI_DEASSERT_ALL & ~SPI_DEASSERTNUM_SSEL(1UL));
    size_t sent         = 0U;
    size_t received     = 0U;

    base->FIFOCFG |= SPI_FIFOCFG_EMPTYTX_MASK | SPI_FIFOCFG_EMPTYRX_MASK;
    base->FIFOSTAT |= SPI_FIFOSTAT_TXERR_MASK | SPI_FIFOSTAT_RXERR_MASK;

    while (received < len)
    {
        uint32_t stat = base->FIFOSTAT;

        if ((stat & SPI_FIFOSTAT_RXNOTEMPTY_MASK) != 0U)
        {
            uint32_t data = base->FIFORD;

            if (rx != NULL)
            {
                rx[received] = (uint8_t)data;
            }
            received++;
        }
        /* Never have more frames in flight than the RX FIFO can hold. */
        if ((sent < len) && ((sent - received) < SD_SPI_PORT_FIFO_DEPTH) && ((stat & SPI_FIFOSTAT_TXNOTFULL_MASK) != 0U))
        {
            base->FIFOWR = ctrl | ((tx != NULL) ? (uint32_t)tx[sent] : 0xFFUL);
            sent++;
        }
    }

    return true;
}
#else
static bool sd_spi_exchange_polling(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transfer_t xfer = {0};

    /* The SPI driver rejects a transfer with neither buffer; clock out 0xFF in small chunks instead. */
    if ((tx == NULL) && (rx == NULL))
    {
        uint8_t idle[16];

        (void)memset(idle, 0xFF, sizeof(idle));
        while (len != 0U)
        {
            size_t chunk = (len < sizeof(idle)) ? len : sizeof(idle);

            if (!sd_spi_exchange_polling(idle, NULL, chunk))
            {
                return false;
            }
            len -= chunk;
        }
        return true;
    }

    xfer.txData      = tx;
    xfer.rxData      = rx;
    xfer.dataSize    = len;
    xfer.configFlags = 0U;

    return SPI_MasterTransferBlocking(SD_SPI_PORT_BASE, &xfer) == kStatus_Success;
}
#endif /* SD_SPI_PORT_FAST_XFER */

#if SD_SPI_PORT_DMA
/* One DMA transfer of at most DMA_MAX_TRANSFER_COUNT bytes; waits for completion. */
static bool sd_spi_exchange_dma(const uint8_t *tx, uint8_t *rx, size_t len)
{
    spi_transfer_t xfer = {0};
    bool done;

    xfer.txData      = tx;
    xfer.rxData      = rx;
    xfer.dataSize    = len;
    xfer.configFlags = 0U;

#if SD_SPI_PORT_FREERTOS
    (void)xSemaphoreTake(s_dmaDone, 0U); /* drop a stale completion */
#else
    s_dmaDone = false;
#endif

    if (SPI_MasterTransferDMA(SD_SPI_PORT_BASE, &s_spiDma, &xfer) != kStatus_Success)
    {
        return false;
    }

#if SD_SPI_PORT_FREERTOS
    done = (xSemaphoreTake(s_dmaDone, pdMS_TO_TICKS(SD_SPI_PORT_DMA_TIMEOUT_MS)) == pdTRUE);
#else
    {
        uint32_t start = SD_SPI_PortTimeStart();

        while (!s_dmaDone && !SD_SPI_PortTimeExpired(start, SD_SPI_PORT_DMA_TIMEOUT_MS))
        {
        }
        done = s_dmaDone;
    }
#endif

    if (!done)
    {
        SPI_MasterTransferAbortDMA(SD_SPI_PORT_BASE, &s_spiDma);
    }
    return done;
}
#endif /* SD_SPI_PORT_DMA */

bool SD_SPI_PortExchange(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (len == 0U)
    {
        return true;
    }

#if SD_SPI_PORT_DMA
    /* DMA only pays off for block payloads; the SDK DMA driver also needs at least one buffer. */
    if (s_dmaEnabled && (len >= SD_SPI_PORT_DMA_MIN_LEN) && ((tx != NULL) || (rx != NULL))
#if SD_SPI_PORT_FREERTOS
        && (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
#endif
    )
    {
        while (len != 0U)
        {
            size_t chunk = (len < SD_SPI_PORT_DMA_MAX_LEN) ? len : SD_SPI_PORT_DMA_MAX_LEN;

            if (!sd_spi_exchange_dma(tx, rx, chunk))
            {
                return false;
            }
            tx = (tx != NULL) ? (tx + chunk) : NULL;
            rx = (rx != NULL) ? (rx + chunk) : NULL;
            len -= chunk;
        }
        return true;
    }
#endif

    return sd_spi_exchange_polling(tx, rx, len);
}

uint8_t SD_SPI_PortXferByte(uint8_t tx)
{
    uint8_t rx = 0xFFU;

    (void)sd_spi_exchange_polling(&tx, &rx, 1U);
    return rx;
}

void SD_SPI_PortDelayMs(uint32_t ms)
{
#if SD_SPI_PORT_FREERTOS
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
    {
        TickType_t ticks = pdMS_TO_TICKS(ms);

        vTaskDelay((ticks == 0U) ? 1U : ticks);
        return;
    }
#endif
    while (ms-- != 0U)
    {
        SDK_DelayAtLeastUs(1000U, SystemCoreClock);
    }
}

void SD_SPI_PortLock(void)
{
#if SD_SPI_PORT_FREERTOS
    if (xTaskGetSchedulerState() != taskSCHEDULER_RUNNING)
    {
        return;
    }
    if (s_busMutex == NULL)
    {
        /* Created on first use. The first card access must come from a single task (it does: SD_Init). */
        s_busMutex = xSemaphoreCreateRecursiveMutex();
    }
    (void)xSemaphoreTakeRecursive(s_busMutex, portMAX_DELAY);
#endif
}

void SD_SPI_PortUnlock(void)
{
#if SD_SPI_PORT_FREERTOS
    if ((xTaskGetSchedulerState() == taskSCHEDULER_RUNNING) && (s_busMutex != NULL))
    {
        (void)xSemaphoreGiveRecursive(s_busMutex);
    }
#endif
}

int SD_SPI_PortCardDetect(void)
{
#if SD_SPI_PORT_CD_ENABLE
    uint32_t level = GPIO_PinRead(GPIO, SD_SPI_PORT_CD_PORT, SD_SPI_PORT_CD_PIN);

    return (level == SD_SPI_PORT_CD_ACTIVE_LEVEL) ? 1 : 0;
#else
    return -1;
#endif
}

uint32_t SD_SPI_PortTimeStart(void)
{
    return MSDK_GetCpuCycleCount();
}

bool SD_SPI_PortTimeExpired(uint32_t start, uint32_t ms)
{
    uint64_t limit = (uint64_t)ms * (SystemCoreClock / 1000U);

    return (uint64_t)(MSDK_GetCpuCycleCount() - start) >= limit;
}

uint32_t SD_SPI_PortElapsedUs(uint32_t start)
{
    uint64_t cycles = (uint32_t)(MSDK_GetCpuCycleCount() - start);

    return (uint32_t)((cycles * 1000000ULL) / SystemCoreClock);
}
