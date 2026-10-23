# CpuTemp

極輕量的 Windows 工作列 CPU 溫度工具。用原生 C / Win32 撰寫，執行時只占約 1–3 MB 記憶體，CPU 使用率接近 0%。

A tiny native Windows tray tool that shows the Intel CPU package temperature (and per-core temperatures in the tooltip).

## 下載

到 [Releases](https://github.com/Shinoed/CpuTemp/releases) 下載最新的 `CpuTemp_Setup_<version>.exe` 並執行。程式碼簽章說明見 [Code signing policy](#code-signing-policy)。

## 功能

- 工作列圖示直接顯示 CPU 封裝溫度，70 °C 以上變黃、85 °C 以上變紅
- 滑鼠停在圖示上顯示每個核心的溫度與 TjMax
- 第二個圖示顯示 CPU 使用率 (藍色數字 + 負載條，70 % 以上變黃、90 % 以上變紅)，可從右鍵選單關閉 *(尚未發布，見下方「數位簽章」)*
- 依螢幕 DPI 自動縮放、跟隨淺色 / 深色工作列換色
- 有安裝 Dell Command | Monitor 的 Dell 電腦，多一個綠色圖示顯示風扇轉速 (千 RPM，例如 2.7 = 2700 RPM)，可從右鍵選單關閉；溫度提示文字也會列出確切轉速。沒裝就不顯示
- 更新頻率可選 1 / 2 / 5 / 10 秒，和是否顯示 CPU 使用率一樣會記住
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

發布新版：把 `CpuTemp.iss` 的 `AppVersion` 和 `cputemp.rc` 的版本改好，推送 `v<版本>` tag (必須與 `AppVersion` 相同)，CI 會建立 draft Release，確認後手動發布。

設定好下列 repository 變數與 secret 後，版本 tag 的建置會先簽 exe 再打包，最後簽安裝檔：

| 名稱 | 類型 | 預設 |
|---|---|---|
| `SIGNPATH_API_TOKEN` | secret | — |
| `SIGNPATH_ORGANIZATION_ID` | variable | — (未設定時跳過簽章) |
| `SIGNPATH_PROJECT_SLUG` | variable | `CpuTemp` |
| `SIGNPATH_POLICY_SLUG` | variable | `release-signing` |
| `SIGNPATH_EXE_CONFIG` / `SIGNPATH_SETUP_CONFIG` | variable | `exe` / `setup` |

SignPath 專案的兩個 artifact configuration (GitHub artifact 是 zip，內含單一檔案)：

```xml
<!-- exe -->
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <zip-file>
    <pe-file path="CpuTempNative.exe"><authenticode-sign/></pe-file>
  </zip-file>
</artifact-configuration>

<!-- setup -->
<artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
  <zip-file>
    <pe-file path="CpuTemp_Setup_*.exe"><authenticode-sign/></pe-file>
  </zip-file>
</artifact-configuration>
```

## 檔案

| 檔案 | 說明 |
|---|---|
| `cputemp.c` | 工作列程式 |
| `usage.h`, `test_usage.c` | CPU 使用率計算與其單元測試 (CI 會執行) |
| `test_icons.c` | 用程式本身的繪圖碼畫出各尺寸圖示、檢查數字大小並輸出預覽圖 (CI 會執行，預覽圖在 Artifacts 的 icon-previews) |
| `fan.h`, `test_fan.c` | 風扇轉速換算、提示文字格式與其單元測試 (CI 會執行) |
| `smoke_test.ps1` | CI 的安裝後測試：靜默安裝、操作執行中的程式 (含模擬的 Dell 風扇感應器)、解除安裝 |
| `cputemp.rc`, `app.manifest` | 資源：manifest、版本資訊、內嵌的 IntelMSR 模組 |
| `cputemp.ico`, `make_ico.ps1` | 程式與安裝檔圖示，及產生它的腳本 (`.\make_ico.ps1 -Out cputemp.ico`) |
| `IntelMSR.bin` | PawnIO 的 IntelMSR 模組 ([PawnIO.Modules](https://github.com/namazso/PawnIO.Modules)) |
| `CpuTemp.iss` | Inno Setup 安裝程式腳本 |
| `task.ps1` | 安裝時建立開機自動啟動的排程工作 |
| `uninstall.ps1` | 解除安裝 (登錄在「設定 > 應用程式」) |

## 數位簽章

目前的執行檔沒有數位簽章。開啟「智慧型應用程式控制」(Smart App Control) 的 Windows 11 可能會封鎖未簽章的新版本，因此 CPU 使用率功能的程式碼已完成但尚未發布，等取得程式碼簽章後再發布。

## Code signing policy

Free code signing provided by [SignPath.io](https://signpath.io), certificate by [SignPath Foundation](https://signpath.org).

Only binaries built by the [GitHub Actions workflow](.github/workflows/build.yml) from the source code in this repository are signed: `CpuTempNative.exe` and `CpuTemp_Setup_<version>.exe`. Third-party components bundled in the setup — the PawnIO installer (signed by its author, namazso) and the PawnIO IntelMSR module — are not signed by this project.

| Role | Members |
|---|---|
| Committers and reviewers | [Contributors](https://github.com/Shinoed/CpuTemp/graphs/contributors) |
| Approvers | [Shinoed](https://github.com/Shinoed) (owner) |

All team members use multi-factor authentication for GitHub and SignPath.

### Privacy policy

This program will not transfer any information to other networked systems unless specifically requested by the user or the person installing or operating it. CpuTemp only reads CPU temperature and usage locally; the app, its setup script and its PowerShell helpers contain no network code.

PawnIO is a third-party driver; see [pawnio.eu](https://pawnio.eu) for its terms.

Security issues: see [SECURITY.md](SECURITY.md). Version history: see [CHANGELOG.md](CHANGELOG.md).

## 授權

本專案以 [MIT License](LICENSE) 授權。

`IntelMSR.bin` 來自 [PawnIO.Modules](https://github.com/namazso/PawnIO.Modules)，依其原授權條款散布，見 [IntelMSR.LICENSE.txt](IntelMSR.LICENSE.txt)。PawnIO 驅動程式為 namazso 的作品，不包含在本儲存庫中。
