# frdmrw612_sdspi_fatfs

FRDM-RW612 以 Flexcomm1 SPI 連接 microSD（SPI mode），搭配 SDK 的 FatFs，裸機（bare-metal）測試程式。
MCUXpresso SDK 26.09.00，armgcc，CMake + Kconfig。

## 接線

全部為 3.3 V，不需 level shifter。

| 訊號 | RW612 腳位 | FRDM-RW612 header | microSD |
|---|---|---|---|
| SCK | GPIO7 | J2 pin 12 | CLK |
| MOSI | GPIO9 | J1 pin 2 | CMD (DI) |
| MISO | GPIO8 | J1 pin 4 | DAT0 (DO) |
| CS | GPIO6（一般 GPIO） | J2 pin 6 | CD/DAT3 |
| 3.3V | — | J2 pin 16 | VDD |
| GND | — | J2 pin 14 | VSS |

- GPIO7/8/9 同時接到 mikroBUS 的 SPI，測試時 mikroBUS 不要插模組。
- 接線盡量短（10 cm 以內）；杜邦線太長時請降低 `APP_SD_MAX_HZ`。

## Pull-up

- 程式已啟用 RW612 內部 pull-up：CS（GPIO6）與 MISO（GPIO8）。
- 內部 pull-up 較弱，若初始化不穩，請在 MISO 與 CS 各加外部 47 kΩ 到 3.3 V。
- microSD 的 DAT1、DAT2 未使用，請各接 47 kΩ pull-up 到 3.3 V（多數轉接板已內建）。

## SPI clock

- Flexcomm1 時鐘來源：FFRO，約 48.3 MHz。
- 初始化階段：除數 121，約 399 kHz（≤ 400 kHz）。
- 初始化後：不超過 `APP_SD_MAX_HZ` 的最大整數分頻。目前 `sd_test.h` 設為 25000000，實際約 24.15 MHz；設 20000000 時實際約 16.1 MHz。
- 實際頻率會在 console 的 `SPI clock` 一行印出。
- SPI 設定：Master、Mode 0、8-bit、MSB first；只讀時 MOSI 送 0xFF。

## 測試選項

在 [source/sd_test.h](source/sd_test.h)：

| 巨集 | 預設 | 說明 |
|---|---|---|
| `APP_CMD0_TEST_ONLY` | 0 | 設 1 只做 CMD0 測試（接線驗證用），不初始化卡、不掛載 |
| `APP_FORMAT_IF_NO_FILESYSTEM` | 0 | 設 1 時，若卡上沒有 FAT 檔案系統就格式化。**會清除整張卡** |
| `APP_THROUGHPUT_TEST` | 1 | 寫入並讀回 1 MB 的 `speed.bin`，polling 與 DMA 各跑一次，印出 KB/s |
| `APP_SD_MAX_HZ` | 25000000 | 初始化後的 SPI clock 上限（實際約 24.15 MHz） |
| `APP_HOTPLUG` | 1 | 測試後持續監看卡片，拔卡時卸載，插卡時重跑測試 |

在 [source/sdspi/sd_spi_port.h](source/sdspi/sd_spi_port.h)：

| 巨集 | 預設 | 說明 |
|---|---|---|
| `SD_SPI_PORT_DMA` | 1（由 CMakeLists.txt 設定） | 編入 SPI DMA，可用 `SD_SPI_PortSetDmaEnabled()` 在執行期切換 |
| `SD_SPI_PORT_FAST_XFER` | 1 | 專案自己的 FIFO polling 迴圈；0 為 SDK 函式 |
| `SD_SPI_PORT_CD_ENABLE` | 0 | 1 表示有接 card detect 開關（預設 GPIO11 / J1 pin 6，插卡時接地）；0 則用 CMD13 探測 |

## Build 與燒錄

VS Code（MCUXpresso for VS Code）：

1. 在 Projects 面板選 `frdmrw612_sdspi_fatfs`，build configuration 選 `flash_debug`，按 Build。
2. USB 接到板上的 MCU-Link 埠，按 Debug（F5）即可燒錄並執行。
3. 序列埠：MCU-Link VCOM，115200 8N1。

命令列（在專案資料夾內，環境變數由 `mcux_include.json` 提供）：

```
cmake --preset flash_debug
cmake --build --preset flash_debug
C:\nxp\LinkServer_26.9.130\LinkServer.exe flash RW612:FRDM-RW612 load flash_debug\frdmrw612_sdspi_fatfs.elf
```

`debug` 這個 preset 是 RAM 映像，只能由 debugger 載入執行，重新上電後不會保留；要留在板子上請用 `flash_debug` 或 `flash_release`。

## 預期輸出

數值依卡片而異：

```
*** SD Card SPI Test on FRDM-RW612 (MCUXpresso SDK) ***
Card type : SDHC
Capacity  : 29508 MB (60432384 sectors)
OCR       : 0xC0FF8000
CID       : MID=0x27 OID=PH PNM=SD32G PRV=6.0 PSN=0x000002F9 MDT=2021/09
SPI clock : 24150943 Hz
Mounted 4:, FAT type 3
Wrote 41 bytes to hello.txt
Read 41 bytes from hello.txt: Hello from MCUXpresso SDK on FRDM-RW612!
Compare: OK
[polling] Write: 1024 KB in 387 ms = 2642 KB/s
[polling] Read : 1024 KB in 379 ms = 2698 KB/s
[polling] Verify: OK
[DMA] Write: 1024 KB in 424 ms = 2410 KB/s
[DMA] Read : 1024 KB in 416 ms = 2456 KB/s
[DMA] Verify: OK
Root directory:
          13  readme.txt
          41  hello.txt
     1048576  speed.bin
Test completed.
Hot-plug monitor: no card-detect switch, probing with CMD13. Remove or insert the card.

Card removed.

Card inserted.
...
Test completed.
```

`APP_CMD0_TEST_ONLY=1` 時：

```
*** SD Card SPI Test on FRDM-RW612 (MCUXpresso SDK) ***
Flexcomm1 clock: 48301886 Hz, SPI clock: 399189 Hz
CMD0 attempt 1, rx: FF 01
CMD0 OK: R1 = 0x01 (card is in idle state)
```

## 檔案

| 檔案 | 內容 |
|---|---|
| [source/main.c](source/main.c) | 裸機進入點 |
| [source/sd_test.c](source/sd_test.c) | 測試流程與熱插拔監看，裸機與 FreeRTOS 共用 |
| [source/sdspi/sd_spi_port.c](source/sdspi/sd_spi_port.c) | Flexcomm1 SPI、DMA、CS GPIO、頻率設定、匯流排鎖、計時（DWT cycle counter） |
| [source/sdspi/sd_spi.c](source/sdspi/sd_spi.c) | SD-SPI 驅動：初始化、CMD17/18 讀、CMD24/25 寫、CSD/CID |
| [source/fatfs/sdspi_disk.c](source/fatfs/sdspi_disk.c) | FatFs diskio 接合層 |
| [source/fatfs/fsl_sdspi_disk.h](source/fatfs/fsl_sdspi_disk.h) | 接合層標頭。檔名不可改，SDK 的 `diskio.c` 以此檔名 include |
| [source/fatfs_config/ffconf.h](source/fatfs_config/ffconf.h) | FatFs 設定（裸機，`FF_FS_REENTRANT=0`） |
| [frdmrw612/hello_world/pin_mux.c](frdmrw612/hello_world/pin_mux.c) | 腳位設定（手動修改，見下） |
| [frdmrw612/prj.conf](frdmrw612/prj.conf) | 加入 `driver.flexcomm_spi`、`driver.flexcomm_spi_dma`、`driver.lpc_dma` 與 `middleware.fatfs` |
| [CMakeLists.txt](CMakeLists.txt) | 原始檔、stack 8 KB、`PRINTF_ADVANCED_ENABLE` |

FreeRTOS 版本在隔壁的 `frdmrw612_sdspi_fatfs_freertos`，它直接編譯本專案的 `source/`，兩個資料夾必須放在一起。

SDK 目錄沒有任何修改。FatFs 的 `ff.c`、`diskio.c` 直接使用 SDK 內的檔案。

## 已知限制

- 實機驗證範圍：2026-11-11 以一張 32 GB SDHC 卡（FAT32）、`debug` preset、SPI 24.15 MHz 跑完整測試通過，包含 polling、DMA 與拔插卡後重跑。SDSC（v1）卡、SDXC 卡、格式化（`f_mkfs`）路徑、card detect 開關、傳輸中拔卡與 `flash_debug` 映像尚未在實機測過。
- 只支援 FAT16/FAT32（`FF_FS_EXFAT=0`）。64 GB 以上的 SDXC 卡出廠為 exFAT，需先格式化為 FAT32。
- 不支援 MMC 卡。
- 資料 CRC16 未檢查（SPI mode 預設關閉 CRC）；命令 CRC7 有送出。
- 裸機下 DMA 也是忙等完成，沒有好處，而且比 polling 慢約 9%（寫 2410 對 2642 KB/s、讀 2456 對 2698 KB/s）。DMA 的價值在 FreeRTOS 下釋放 CPU，見該專案的 readme。
- 資料傳輸使用專案自己的 FIFO polling 迴圈（`SD_SPI_PORT_FAST_XFER=1`，固定以 `-O2` 編譯）。SDK 的 `SPI_MasterTransferBlocking` 在 `-O0` 下每 byte 約需 125 個 CPU cycle，超過約 16 MHz 後讀取速度不再提升（實測 24.15 MHz 只有 1767 KB/s）；設為 0 可切回 SDK 函式。
- 24.15 MHz 時讀取的資料階段已等於線上傳輸時間（每 block 約 170 µs），再快只能提高 SPI clock；DMA 不會提升吞吐量，只會釋放 CPU。
- 熱插拔預設以 CMD13 每 500 ms 探測，只在測試流程之外偵測。傳輸中拔卡會以 FatFs 錯誤結束該次操作，寫入中的檔案可能損毀。
- 無 RTC，檔案時間固定（`FF_FS_NORTC=1`）。
- 檔案系統磁碟代號是 `4:`（SDK `diskio.h` 的 `SDSPIDISK`）。
- `pin_mux.c` 是手動修改的：Flexcomm1 只切 GPIO7/8/9，不輸出硬體 SSEL。用 MCUXpresso Config Tools 重新產生 `pin_mux.c` 會蓋掉這段。
- SDHC/SDXC 的 `GET_BLOCK_SIZE` 回報固定值 8192 sectors（4 MiB），只影響 `f_mkfs` 的對齊。
- 逾時值：ACMD41 1 s、讀取 token 100 ms、寫入 busy 1 s，定義在 [source/sdspi/sd_spi.h](source/sdspi/sd_spi.h)。少數卡片初始化超過 1 s，可調大 `SD_SPI_ACMD41_TIMEOUT_MS`。

## 除錯

驅動在錯誤時會印出 `[sd] CMDn: R1=0x.. <原因>`。

**讀到全 0xFF（`no response`）**

- 3.3 V 與 GND 是否接到卡上，卡是否插到底。
- MISO 是否接到 DAT0（J1 pin 4），MOSI 是否接到 CMD（J1 pin 2）；這兩條最容易接反。
- CS 是否接到 CD/DAT3（J2 pin 6），SCK 是否接到 CLK（J2 pin 12）。
- 用示波器或邏輯分析儀看 SCK 是否約 400 kHz、CS 在命令期間是否為低。

**R1 不是 0x01**

- `0x00`：MISO 被拉低或短路到 GND；或卡片先前已初始化過而未斷電，重新上電再試。
- `0x05`（idle + illegal command）出現在 CMD8：舊版 v1 卡，屬正常，驅動會自動處理。
- `0x09` 或含 `crc-error`：訊號品質問題，縮短接線、檢查 GND。
- 每次數值都不同：MISO 浮接，補上外部 pull-up；或接線太長。
- 第一次失敗、重試成功：上電後電源尚未穩定，屬正常。

**ACMD41 timeout**

- 卡片供電不足：初始化時電流較大，確認 3.3 V 沒有明顯壓降，必要時在卡座旁加 10 µF 電容。
- 卡片初始化較慢：把 `SD_SPI_ACMD41_TIMEOUT_MS` 調到 2000 以上。
- 出現 `illegal-command`：是 MMC 卡，不支援。

**初始化成功但讀寫出錯（`data token timeout`、`data response ... rejected`、`FR_DISK_ERR`）**

- 高速 clock 下訊號品質不足：把 `APP_SD_MAX_HZ` 降到 8000000 或 4000000 再試。
- 接線過長或沒有共地。

**`f_mount` 回 `FR_NO_FILESYSTEM`**

- 卡片不是 FAT16/FAT32（常見為 exFAT）。在 PC 上格式化為 FAT32，或把 `APP_FORMAT_IF_NO_FILESYSTEM` 設為 1。
