# CpuTemp

極輕量的 Windows 工作列 CPU 溫度工具。用原生 C / Win32 撰寫，執行時只占約 1–3 MB 記憶體，CPU 使用率接近 0%。

A tiny native Windows tray tool that shows the Intel CPU package temperature (and per-core temperatures in the tooltip).

## 功能

- 工作列圖示直接顯示 CPU 封裝溫度，70 °C 以上變黃、85 °C 以上變紅
- 滑鼠停在圖示上顯示每個核心的溫度與 TjMax
- 第二個圖示顯示 CPU 使用率 (藍色數字 + 負載條)，可從右鍵選單關閉 *(尚未發布，見下方「數位簽章」)*
- 依螢幕 DPI 自動縮放、跟隨淺色 / 深色工作列換色
- 更新頻率可選 1 / 2 / 5 / 10 秒
- 安裝程式自動檢查需求、在背景安裝 PawnIO 驅動、設定登入時自動啟動；同一個安裝檔也能解除安裝

## 系統需求

- Windows 10 / 11 64 位元
- Intel CPU
- [PawnIO](https://pawnio.eu) 驅動程式 (安裝程式會自動安裝)

不需要 .NET 或 Visual C++ 執行階段。

## 運作方式

透過 PawnIO 驅動與其 IntelMSR 模組 (內嵌在 exe 資源中) 讀取 MSR：

| MSR | 用途 |
|---|---|
| `IA32_TEMPERATURE_TARGET` (0x1A2) | bits 23:16 = TjMax |
| `IA32_PACKAGE_THERM_STATUS` (0x1B1) | bits 22:16 = 封裝距 TjMax 的度數 |
| `IA32_THERM_STATUS` (0x19C) | bits 22:16 = 各核心距 TjMax 的度數 (逐核心切換執行緒親和性讀取) |

讀取 MSR 需要系統管理員權限，因此程式以 `requireAdministrator` 執行，開機自動啟動透過「登入時以最高權限執行」的排程工作。

## 建置

需要：

- [w64devkit](https://github.com/skeeto/w64devkit) (gcc + windres) 在 `PATH` 中
- [Inno Setup](https://jrsoftware.org/isinfo.php) (`ISCC.exe`) 在 `PATH` 中
- PawnIO 安裝檔 `PawnIO_setup.exe` ([PawnIO.Setup releases](https://github.com/namazso/PawnIO.Setup/releases)) 放在專案根目錄

```bat
build_c.bat        :: 產生 CpuTempNative.exe
build_setup.bat    :: 產生 dist\CpuTemp_Setup_<version>.exe
```

GitHub Actions ([`.github/workflows/build.yml`](.github/workflows/build.yml)) 會在每次 push 時用固定版本的 w64devkit 2.10.1、Inno Setup 7.1.0、PawnIO 2.2.0 (皆驗證 SHA-256) 自動建置，產物可在 Actions 頁面的 Artifacts 下載。

## 檔案

| 檔案 | 說明 |
|---|---|
| `cputemp.c` | 工作列程式 |
| `cputemp.rc`, `app.manifest` | 資源：manifest、版本資訊、內嵌的 IntelMSR 模組 |
| `IntelMSR.bin` | PawnIO 的 IntelMSR 模組 ([PawnIO.Modules](https://github.com/namazso/PawnIO.Modules)) |
| `CpuTemp.iss` | Inno Setup 安裝程式腳本 |
| `task.ps1` | 安裝時建立開機自動啟動的排程工作 |
| `uninstall.ps1` | 解除安裝 (登錄在「設定 > 應用程式」) |

## 數位簽章

目前的執行檔沒有數位簽章。開啟「智慧型應用程式控制」(Smart App Control) 的 Windows 11 可能會封鎖未簽章的新版本，因此 CPU 使用率功能的程式碼已完成但尚未發布，等取得程式碼簽章後再發布。

## 授權

本專案以 [MIT License](LICENSE) 授權。

`IntelMSR.bin` 來自 [PawnIO.Modules](https://github.com/namazso/PawnIO.Modules)，依其原授權條款散布，見 [IntelMSR.LICENSE.txt](IntelMSR.LICENSE.txt)。PawnIO 驅動程式為 namazso 的作品，不包含在本儲存庫中。
