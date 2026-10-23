# CpuTemp installer
#  - Installs the PawnIO driver if it is missing
#  - Copies CpuTempNative.exe to Program Files\CpuTemp
#  - Registers a logon task (highest privileges) so it starts with Windows
#  - Adds an entry to Settings > Apps so it can be uninstalled from there
# Run via Install.bat (it elevates automatically).

$ErrorActionPreference = 'Stop'
$AppName = 'CpuTemp'
$Version = '1.0.0'
$Dir     = Join-Path $env:ProgramFiles $AppName
$Exe     = Join-Path $Dir 'CpuTempNative.exe'
$Task    = 'CpuTempTray'
$ArpKey  = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName"
$Src     = $PSScriptRoot

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

if (-not ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrators')) {
    throw 'Administrator rights are required. Please run Install.bat.'
}
if ((Get-CimInstance Win32_Processor | Select-Object -First 1).Manufacturer -ne 'GenuineIntel') {
    throw 'Only Intel CPUs are supported.'
}
foreach ($f in 'CpuTempNative.exe', 'uninstall.ps1') {
    if (-not (Test-Path (Join-Path $Src $f))) { throw "Missing $f next to the installer (build it with build_c.bat first)." }
}

# 1. PawnIO driver
Step 'Checking PawnIO driver'
if (-not (Get-Service PawnIO -ErrorAction SilentlyContinue)) {
    $setup = Join-Path $Src 'PawnIO_setup.exe'
    if (-not (Test-Path $setup)) { throw 'PawnIO is not installed and PawnIO_setup.exe was not found.' }
    Write-Host '    Not installed - launching PawnIO setup, please complete it.'
    Start-Process $setup -Wait
    if (-not (Get-Service PawnIO -ErrorAction SilentlyContinue)) { throw 'PawnIO installation was not completed.' }
}
Write-Host '    PawnIO OK'

# 2. Files
Step "Copying files to $Dir"
Get-Process CpuTempNative -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 300
New-Item -ItemType Directory -Force $Dir | Out-Null
Copy-Item (Join-Path $Src 'CpuTempNative.exe') $Exe -Force
Copy-Item (Join-Path $Src 'uninstall.ps1') $Dir -Force
if (Test-Path (Join-Path $Src 'IntelMSR.LICENSE.txt')) { Copy-Item (Join-Path $Src 'IntelMSR.LICENSE.txt') $Dir -Force }

# 3. Logon task
Step 'Registering startup task'
$user      = "$env:USERDOMAIN\$env:USERNAME"
$action    = New-ScheduledTaskAction -Execute $Exe
$trigger   = New-ScheduledTaskTrigger -AtLogOn -User $user
$principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
$settings  = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
                 -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew
Register-ScheduledTask -TaskName $Task -Action $action -Trigger $trigger -Principal $principal `
    -Settings $settings -Description 'CPU temperature tray icon' -Force | Out-Null

# 4. Settings > Apps entry
Step 'Registering in Settings > Apps'
$uninstallCmd = "powershell.exe -NoProfile -ExecutionPolicy Bypass -File `"$Dir\uninstall.ps1`""
New-Item -Path $ArpKey -Force | Out-Null
$props = @{
    DisplayName          = 'CpuTemp (CPU temperature tray)'
    DisplayVersion       = $Version
    Publisher            = 'CpuTemp'
    DisplayIcon          = $Exe
    InstallLocation      = $Dir
    UninstallString      = $uninstallCmd
    QuietUninstallString = "$uninstallCmd -Quiet"
}
foreach ($k in $props.Keys) { Set-ItemProperty $ArpKey $k $props[$k] }
Set-ItemProperty $ArpKey NoModify 1 -Type DWord
Set-ItemProperty $ArpKey NoRepair 1 -Type DWord
Set-ItemProperty $ArpKey EstimatedSize ([int][math]::Ceiling((Get-ChildItem $Dir | Measure-Object Length -Sum).Sum / 1KB)) -Type DWord

# 5. Start now
Step 'Starting'
Start-ScheduledTask -TaskName $Task
Write-Host ''
Write-Host "Installed $AppName $Version. It will start automatically when you sign in." -ForegroundColor Green
