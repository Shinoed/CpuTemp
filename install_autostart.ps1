# Install CpuTempNative to Program Files and run it at logon with highest privileges.
# Run from an elevated PowerShell:  powershell -ExecutionPolicy Bypass -File install_autostart.ps1
# Uninstall:                        powershell -ExecutionPolicy Bypass -File install_autostart.ps1 -Uninstall
param([switch]$Uninstall)

$ErrorActionPreference = 'Stop'
$dir  = Join-Path $env:ProgramFiles 'CpuTemp'
$exe  = Join-Path $dir 'CpuTempNative.exe'
$task = 'CpuTempTray'

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrators')) {
    throw 'Please run this script from an elevated (Administrator) PowerShell.'
}

if ($Uninstall) {
    Get-Process CpuTempNative -ErrorAction SilentlyContinue | Stop-Process -Force
    Unregister-ScheduledTask -TaskName $task -Confirm:$false -ErrorAction SilentlyContinue
    Remove-Item $dir -Recurse -Force -ErrorAction SilentlyContinue
    Write-Host 'Uninstalled.'
    return
}

# Stop the running copy so the exe can be replaced
Get-Process CpuTempNative -ErrorAction SilentlyContinue | Stop-Process -Force
New-Item -ItemType Directory -Force $dir | Out-Null
Copy-Item (Join-Path $PSScriptRoot 'CpuTempNative.exe') $exe -Force
Copy-Item (Join-Path $PSScriptRoot 'IntelMSR.LICENSE.txt') $dir -Force

$user      = "$env:USERDOMAIN\$env:USERNAME"
$action    = New-ScheduledTaskAction -Execute $exe
$trigger   = New-ScheduledTaskTrigger -AtLogOn -User $user
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
$settings  = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
                 -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $task -Action $action -Trigger $trigger -Principal $principal `
    -Settings $settings -Description 'CPU temperature tray icon' -Force | Out-Null

Start-ScheduledTask -TaskName $task
Write-Host "Installed to $dir and registered logon task '$task'. It is running now."
