# CpuTemp uninstaller (installed to Program Files\CpuTemp, registered in Settings > Apps).
# A script run by the Microsoft-signed powershell.exe is used instead of Inno Setup's
# unsigned unins000.exe, which Smart App Control blocks.
#   -Quiet         no dialogs; keeps PawnIO unless -RemovePawnIO
#   -RemovePawnIO  also remove the PawnIO driver without asking
#   -NoConfirm     skip "uninstall CpuTemp?" (setup already asked)
#   -InstallDir / -KeyRoot  override locations (for testing)
# A log is written to %TEMP%\CpuTemp_uninstall.log.
param([switch]$Quiet, [switch]$RemovePawnIO, [switch]$NoConfirm,
      [string]$InstallDir = (Join-Path $env:ProgramFiles 'CpuTemp'),
      [string]$KeyRoot = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall')

# Setup (a 32-bit process) can start the 32-bit PowerShell, which sees Program Files (x86)
# and the WOW6432Node registry view, so nothing of the 64-bit install was found.
# Re-run in 64-bit PowerShell with the same arguments.
if ([Environment]::Is64BitOperatingSystem -and -not [Environment]::Is64BitProcess) {
    $ps64 = Join-Path $env:WINDIR 'sysnative\WindowsPowerShell\v1.0\powershell.exe'
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-WindowStyle', 'Hidden', '-File', "`"$PSCommandPath`"")
    foreach ($p in $PSBoundParameters.GetEnumerator()) {
        if ($p.Value -is [switch]) { if ($p.Value) { $argList += "-$($p.Key)" } }
        else { $argList += "-$($p.Key)", "`"$($p.Value)`"" }
    }
    $proc = Start-Process $ps64 -ArgumentList $argList -Wait -PassThru -WindowStyle Hidden
    exit $proc.ExitCode
}

$AppName = 'CpuTemp'
$Dir     = $InstallDir
$Task    = 'CpuTempTray'
$ArpKeys = "$KeyRoot\$AppName", "$KeyRoot\${AppName}_is1"   # _is1: 1.0.0-1.0.5 (Inno uninstaller)
$PawnUninstaller = Join-Path $env:ProgramFiles 'PawnIO\uninstall.exe'
$Log     = Join-Path $env:TEMP 'CpuTemp_uninstall.log'

function Log($msg) { Add-Content -Path $Log -Value "$(Get-Date -Format 'HH:mm:ss') $msg" -Encoding UTF8 }

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

Log "---- uninstall started (dir=$Dir)"
if (-not $Quiet -and -not $NoConfirm -and -not (Ask "要解除安裝 CpuTemp 嗎？")) { Log 'cancelled'; exit 1 }
if (-not $Quiet -and -not $RemovePawnIO -and (Test-Path $PawnUninstaller)) {
    $RemovePawnIO = Ask "要一併移除 PawnIO 驅動程式嗎？`n`n其他硬體監控工具 (例如 LibreHardwareMonitor、FanControl) 可能也需要它。"
}
Log "remove PawnIO: $RemovePawnIO"

Set-Location $env:TEMP   # do not hold the install folder open
[Environment]::CurrentDirectory = $env:TEMP
$failed = @()
$rebootNeeded = $false

Add-Type -Namespace CpuTemp -Name Win32 -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, string title);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint msg, System.IntPtr w, System.IntPtr l);
[DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)] public static extern bool MoveFileExW(string from, string to, int flags);
'@

# 1. Close the tray app gracefully (removes its icon), force-kill only as a fallback
$procs = @(Get-Process CpuTempNative -ErrorAction SilentlyContinue)
if ($procs) {
    $hwnd = [CpuTemp.Win32]::FindWindowW('CpuTempTray', $null)
    if ($hwnd -ne [IntPtr]::Zero) { [void][CpuTemp.Win32]::PostMessageW($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) }
    $procs | Wait-Process -Timeout 3 -ErrorAction SilentlyContinue
    Get-Process CpuTempNative -ErrorAction SilentlyContinue | Stop-Process -Force -PassThru -ErrorAction SilentlyContinue |
        Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
    if (Get-Process CpuTempNative -ErrorAction SilentlyContinue) { $failed += '無法關閉 CpuTemp' }
}
Log "app stopped: $(-not $failed)"

# 2. Startup task
if (Get-ScheduledTask -TaskName $Task -ErrorAction SilentlyContinue) {
    try { Unregister-ScheduledTask -TaskName $Task -Confirm:$false -ErrorAction Stop; Log 'task removed' }
    catch { $failed += "排程工作：$($_.Exception.Message)"; Log "task: $($_.Exception.Message)" }
}

# 3. Files. Right after the app exits, Windows or antivirus can still hold the exe for a
#    moment, so retry; whatever stays locked is deleted at the next reboot.
if (-not $failed -and (Test-Path $Dir)) {
    for ($i = 1; $i -le 10 -and (Test-Path $Dir); $i++) {
        try { Remove-Item $Dir -Recurse -Force -ErrorAction Stop }
        catch { Log "delete attempt ${i}: $($_.Exception.Message)"; Start-Sleep -Milliseconds 500 }
    }
    if (Test-Path $Dir) {
        $items = @(Get-ChildItem $Dir -Recurse -Force | Sort-Object { $_.FullName.Length } -Descending) + @(Get-Item $Dir)
        foreach ($it in $items) {
            if (-not [CpuTemp.Win32]::MoveFileExW($it.FullName, $null, 4)) {   # MOVEFILE_DELAY_UNTIL_REBOOT
                $failed += "檔案：無法刪除 $($it.FullName)"
            }
        }
        if (-not $failed) { $rebootNeeded = $true; Log 'files scheduled for deletion at reboot' }
    } else { Log 'files removed' }
}

# 4. Settings > Apps entries last, so a failed uninstall can be retried from there
if (-not $failed) {
    foreach ($k in $ArpKeys) {
        Remove-Item $k -Recurse -Force -ErrorAction SilentlyContinue
        if (Test-Path $k) { $failed += '無法移除「設定 > 應用程式」項目'; Log "key left: $k" }
    }
}

# 5. PawnIO, only after CpuTemp no longer uses it
$pawnFailed = $false
if (-not $failed -and $RemovePawnIO -and (Test-Path $PawnUninstaller)) {
    $p = Start-Process $PawnUninstaller -ArgumentList '-uninstall', '-silent' -Wait -PassThru
    $pawnFailed = ($p.ExitCode -ne 0) -or (Test-Path "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO")
    Log "PawnIO uninstall exit=$($p.ExitCode) failed=$pawnFailed"
}

if ($failed) {
    Log "FAILED: $($failed -join ' | ')"
    Tell ("解除安裝沒有完成：`n`n- " + ($failed -join "`n- ") + "`n`n請重新開機後再執行一次解除安裝。`n詳細記錄：$Log") 'Error'
    exit 1
}
$msg = 'CpuTemp 已移除。'
if ($RemovePawnIO -and -not $pawnFailed) { $msg += "`nPawnIO 驅動程式也已移除。" }
if ($rebootNeeded) { $msg += "`n部分檔案會在重新開機後刪除。" }
if ($pawnFailed) {
    Log 'done, PawnIO left'
    Tell ("$msg`n`n但 PawnIO 沒有移除成功，請到「設定 > 應用程式」手動移除 PawnIO，或重新開機後再試。") 'Warning'
    exit 2
}
Log 'done'
Tell $msg 'Information'
exit 0
