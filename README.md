# FRDM-RW612 microSD over SPI + FatFs

MCUXpresso SDK 專案：FRDM-RW612 以 Flexcomm1 SPI 連接 microSD（SPI mode），使用 FatFs。

| 專案 | 內容 |
|---|---|
| [frdmrw612_sdspi_fatfs](frdmrw612_sdspi_fatfs/) | 裸機版，含 SD-SPI 驅動、FatFs 接合層、測試程式。接線與除錯方法見其 readme |
| [frdmrw612_sdspi_fatfs_freertos](frdmrw612_sdspi_fatfs_freertos/) | FreeRTOS 版，直接編譯裸機版的 `source/`，兩個資料夾必須放在一起 |

## 建置前準備

本 repo 不含 SDK。需要 MCUXpresso SDK 26.09.00（FRDM-RW612）與 Arm GNU Toolchain 14.2。

在兩個專案資料夾內各做一次：

1. 把 `mcux_include.json.template` 複製成 `mcux_include.json`。
2. 把其中的 `<TOOLS_DIR>`（通常是 `C:/Users/你的帳號/.mcuxpressotools`）與 `<SDK_ROOT>` 換成本機路徑。
3. 把 `.vscode/mcuxpresso-tools.json` 的 `sdk.path` 改成本機的 SDK 路徑。

然後：

```
cmake --preset flash_debug
cmake --build --preset flash_debug
```
