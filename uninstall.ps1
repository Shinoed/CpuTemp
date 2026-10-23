# CpuTemp uninstaller
# Removes the startup task, the Settings > Apps entry and Program Files\CpuTemp.
# Optionally removes the PawnIO driver (other tools such as LibreHardwareMonitor may also use it).
#   -Quiet        no prompts, keep PawnIO
#   -RemovePawnIO also uninstall PawnIO
param([switch]$Quiet, [switch]$RemovePawnIO)

$AppName = 'CpuTemp'
$Dir     = Join-Path $env:ProgramFiles $AppName
$Task    = 'CpuTempTray'
$ArpKey  = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName"

# Re-launch elevated when started from Settings > Apps without admin rights
if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrators')) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    if ($Quiet) { $argList += '-Quiet' }
    if ($RemovePawnIO) { $argList += '-RemovePawnIO' }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $argList
    return
}

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }
Set-Location $env:TEMP   # do not hold the install folder open

Step 'Stopping CpuTemp'
Get-Process CpuTempNative -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300

Step 'Removing startup task'
Unregister-ScheduledTask -TaskName $Task -Confirm:$false -ErrorAction SilentlyContinue

Step 'Removing Settings > Apps entry'
Remove-Item $ArpKey -Recurse -Force -ErrorAction SilentlyContinue

Step "Removing $Dir"
Remove-Item $Dir -Recurse -Force -ErrorAction SilentlyContinue

$pawnUninstaller = Join-Path $env:ProgramFiles 'PawnIO\uninstall.exe'
if (-not $Quiet -and -not $RemovePawnIO -and (Test-Path $pawnUninstaller)) {
    $ans = Read-Host 'Also remove the PawnIO driver? Other hardware tools may need it. (y/N)'
    $RemovePawnIO = $ans -match '^[yY]'
}
if ($RemovePawnIO -and (Test-Path $pawnUninstaller)) {
    Step 'Launching PawnIO uninstaller'
    Start-Process $pawnUninstaller -Wait
}

Write-Host ''
Write-Host 'CpuTemp has been removed.' -ForegroundColor Green
if (-not $Quiet) { Read-Host 'Press Enter to close' | Out-Null }
