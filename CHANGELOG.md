# Changelog

## [1.1.0] - 尚未發布

等取得 SignPath 程式碼簽章後發布，是第一個有數位簽章的版本。

### 新增
- 第二個工作列圖示顯示 CPU 使用率 (藍色數字 + 負載條)，可從右鍵選單關閉
- exe 版本資訊
- 程式與安裝檔圖示；「設定 > 應用程式」的發行者改為 Shinoed
- GitHub Actions 自動建置、SignPath 簽章、版本 tag 建立 draft Release
- 有安裝 Dell Command | Monitor 時，以第三個工作列圖示 (千 RPM) 和提示文字顯示風扇轉速，可從右鍵選單關閉
- CPU 使用率與風扇轉速的單元測試，以及 CI 的安裝後自動測試
- `SECURITY.md` 說明如何回報安全性問題

### 修正
- 更新頻率在重新啟動後沒有保留，一律回到 2 秒
- 靜默安裝時若 PawnIO 安裝失敗，會跳出需要手動操作的視窗而卡住
- 啟動、Explorer 重啟和切換主題時，CPU 使用率連續取樣間隔太短，導致顯示 `--` 或不準確的數值

## [1.0.8] - 2026-10-23

- 解除安裝改用 64 位元 PowerShell 執行
- 解除安裝時遇到暫時被鎖住的檔案也能完成
- 用 `uninstall.ps1` 取代 Inno 內建的解除安裝程式
- 背景靜默安裝與移除 PawnIO

[1.1.0]: https://github.com/Shinoed/CpuTemp/compare/v1.0.8...HEAD
[1.0.8]: https://github.com/Shinoed/CpuTemp/releases/tag/v1.0.8
