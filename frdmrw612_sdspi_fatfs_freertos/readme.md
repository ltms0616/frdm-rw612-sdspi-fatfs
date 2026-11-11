# frdmrw612_sdspi_fatfs_freertos

FRDM-RW612 以 Flexcomm1 SPI 連接 microSD（SPI mode），搭配 SDK 的 FatFs 與 FreeRTOS。

接線、pull-up、SPI clock、測試選項與除錯方法與裸機版相同，請見 [../frdmrw612_sdspi_fatfs/readme.md](../frdmrw612_sdspi_fatfs/readme.md)。

## 專案結構

驅動、disk 接合層與測試流程不在本專案內，而是直接編譯 `../frdmrw612_sdspi_fatfs/source`，兩個資料夾必須放在一起。本專案只有：

| 檔案 | 內容 |
|---|---|
| [source/main_freertos.c](source/main_freertos.c) | 建立 task、啟動 scheduler |
| [source/fatfs_config/ffconf.h](source/fatfs_config/ffconf.h) | `FF_FS_REENTRANT=1`、`FF_FS_TIMEOUT=5000` |
| [frdmrw612/prj.conf](frdmrw612/prj.conf) | FreeRTOS kernel（CM33 non-TrustZone）、heap_4、32 KB heap、1 kHz tick |
| [CMakeLists.txt](CMakeLists.txt) | `SD_SPI_PORT_FREERTOS=1`、`SD_SPI_PORT_DMA=1` |

## Task

| Task | 優先權 | Stack | 內容 |
|---|---|---|---|
| `sd_reader` | 3 | 4 KB | 吞吐量測試期間每 5 ms 讀一次 hello.txt，驗證 FatFs 重入 |
| `sd_test` | 2 | 8 KB | 測試流程與熱插拔監看 |
| `background` | 1 | 1 KB | 空轉計數，用來量 SD task 留給其他 task 的 CPU |

`sd_reader` 的優先權必須高於 `sd_test`。反過來的話，它在 `sd_test` 連續呼叫 `f_read` 之間得不到執行機會，FatFs 的鎖等於沒被測到。讀取次數為 0 時測試會判定為失敗。

## 執行期行為

- DMA 傳輸時呼叫的 task 阻塞在 semaphore 上，CPU 可給其他 task。
- polling 傳輸不會讓出 CPU，同優先權以下的 task 在傳輸期間不會執行。
- DMA IRQ 優先權設在 FreeRTOS 的 syscall 門檻之下，因為 callback 使用 FromISR API。
- 每次卡片操作由遞迴 mutex 保護，CS 在整個操作期間維持。

## 實測

2026-11-11，32 GB SDHC（FAT32），`debug` preset（`-O0`），SPI 24.15 MHz，包含拔插卡後重跑一次，兩次結果一致：

| 模式 | 寫入 | 讀取 | 留給低優先權 task 的 CPU | `sd_reader` 同時讀取 |
|---|---|---|---|---|
| polling | 約 2000 KB/s | 約 2445 KB/s | 0% | 133 次成功，0 錯誤 |
| DMA | 約 2250 KB/s | 約 2305 KB/s | 寫入 73 到 74%、讀取 75% | 133 次成功，0 錯誤 |

這些速度包含 `sd_reader` 同時存取卡片的干擾。沒有 reader 干擾時，polling 約 2640 / 2697 KB/s，DMA 約 2293 到 2359 / 2415 到 2418 KB/s。

尚未在實機測過：`flash_debug` 映像、傳輸中拔卡、card detect 開關、SDSC 與 SDXC 卡。
