# CpuTemp uninstaller (installed to Program Files\CpuTemp, registered in Settings > Apps).
# A script run by the Microsoft-signed powershell.exe is used instead of Inno Setup's
# unsigned unins000.exe, which Smart App Control blocks.
#   -Quiet         no dialogs; keeps PawnIO unless -RemovePawnIO
#   -RemovePawnIO  also remove the PawnIO driver without asking
#   -NoConfirm     skip "uninstall CpuTemp?" (setup already asked)
param([switch]$Quiet, [switch]$RemovePawnIO, [switch]$NoConfirm)

$AppName = 'CpuTemp'
$Dir     = Join-Path $env:ProgramFiles $AppName
$Task    = 'CpuTempTray'
$ArpKeys = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName",
           "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\${AppName}_is1"  # 1.0.0-1.0.5 (Inno uninstaller)
$PawnUninstaller = Join-Path $env:ProgramFiles 'PawnIO\uninstall.exe'

Add-Type -AssemblyName System.Windows.Forms
function Ask($text) {
    [System.Windows.Forms.MessageBox]::Show($text, $AppName, 'YesNo', 'Question', 'Button2') -eq 'Yes'
}
function Tell($text, $icon) {
    if (-not $Quiet) { [void][System.Windows.Forms.MessageBox]::Show($text, $AppName, 'OK', $icon) }
}

# Settings > Apps may start us without admin rights: re-launch elevated
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrators')) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', "`"$PSCommandPath`"")
    if ($Quiet) { $argList += '-Quiet' }
    if ($RemovePawnIO) { $argList += '-RemovePawnIO' }
    if ($NoConfirm) { $argList += '-NoConfirm' }
    try { Start-Process powershell.exe -Verb RunAs -ArgumentList $argList } catch { }
    exit
}

if (-not $Quiet -and -not $NoConfirm -and -not (Ask "要解除安裝 CpuTemp 嗎？")) { exit 1 }
if (-not $Quiet -and -not $RemovePawnIO -and (Test-Path $PawnUninstaller)) {
    $RemovePawnIO = Ask "要一併移除 PawnIO 驅動程式嗎？`n`n其他硬體監控工具 (例如 LibreHardwareMonitor、FanControl) 可能也需要它。"
}

Set-Location $env:TEMP   # do not hold the install folder open
$failed = @()

# 1. Close the tray app gracefully (removes its icon), force-kill only as a fallback
Add-Type -Namespace CpuTemp -Name Win32 -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, string title);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint msg, System.IntPtr w, System.IntPtr l);
'@
$procs = @(Get-Process CpuTempNative -ErrorAction SilentlyContinue)
if ($procs) {
    $hwnd = [CpuTemp.Win32]::FindWindowW('CpuTempTray', $null)
    if ($hwnd -ne [IntPtr]::Zero) { [void][CpuTemp.Win32]::PostMessageW($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
    $procs | Wait-Process -Timeout 3 -ErrorAction SilentlyContinue
    Get-Process CpuTempNative -ErrorAction SilentlyContinue | Stop-Process -Force -PassThru -ErrorAction SilentlyContinue |
        Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
    if (Get-Process CpuTempNative -ErrorAction SilentlyContinue) { $failed += '無法關閉 CpuTemp' }
}

# 2. Startup task
if (Get-ScheduledTask -TaskName $Task -ErrorAction SilentlyContinue) {
    try { Unregister-ScheduledTask -TaskName $Task -Confirm:$false -ErrorAction Stop }
    catch { $failed += "排程工作：$($_.Exception.Message)" }
}

# 3. Files (this script is already loaded in memory, so it can delete its own folder;
#    setup may also run a copy of it from its temp folder)
if (-not $failed -and (Test-Path $Dir)) {
    try { Remove-Item $Dir -Recurse -Force -ErrorAction Stop }
    catch { $failed += "檔案：$($_.Exception.Message)" }
}

# 4. Settings > Apps entry last, so a failed uninstall can be retried from there
if (-not $failed) {
    foreach ($k in $ArpKeys) {
        Remove-Item $k -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path $k) { $failed += '無法移除「設定 > 應用程式」項目' }
    }
}

# 5. PawnIO, only after CpuTemp no longer uses it
$pawnFailed = $false
if (-not $failed -and $RemovePawnIO -and (Test-Path $PawnUninstaller)) {
    $p = Start-Process $PawnUninstaller -ArgumentList '-uninstall', '-silent' -Wait -PassThru
    $pawnFailed = ($p.ExitCode -ne 0) -or (Test-Path 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO')
}

if ($failed) {
    Tell ("解除安裝沒有完成：`n`n- " + ($failed -join "`n- ") + "`n`n請關閉使用 CpuTemp 資料夾的程式後再試一次。") 'Error'
    exit 1
}
if ($pawnFailed) {
    Tell "CpuTemp 已移除，但 PawnIO 沒有移除成功。`n請到「設定 > 應用程式」手動移除 PawnIO，或重新開機後再試。" 'Warning'
    exit 2
}
Tell ("CpuTemp 已移除。" + $(if ($RemovePawnIO) { "`nPawnIO 驅動程式也已移除。" } else { '' })) 'Information'
exit 0
