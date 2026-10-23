# CpuTemp uninstaller
# Removes the startup task, the Settings > Apps entry and Program Files\CpuTemp.
# Optionally removes the PawnIO driver (other tools such as LibreHardwareMonitor may also use it).
#   -Quiet        no prompts, keep PawnIO
#   -RemovePawnIO also uninstall PawnIO
param([switch]$Quiet, [switch]$RemovePawnIO)

. (Join-Path $PSScriptRoot 'common.ps1')

# Re-launch elevated when started from Settings > Apps without admin rights
if (-not (Test-Admin)) {
    $argList = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', "`"$PSCommandPath`"")
    if ($Quiet) { $argList += '-Quiet' }
    if ($RemovePawnIO) { $argList += '-RemovePawnIO' }
    Start-Process powershell.exe -Verb RunAs -ArgumentList $argList
    return
}

Set-Location $env:TEMP   # do not hold the install folder open
$failed = @()

Step 'Stopping CpuTemp'
try { Stop-CpuTemp } catch { $failed += $_.Exception.Message }

Step 'Removing startup task'
if (Get-ScheduledTask -TaskName $Task -ErrorAction SilentlyContinue) {
    try { Unregister-ScheduledTask -TaskName $Task -Confirm:$false -ErrorAction Stop }
    catch { $failed += "Task: $($_.Exception.Message)" }
}

Step "Removing $Dir"
if (Test-Path $Dir) {
    try { Remove-Item $Dir -Recurse -Force -ErrorAction Stop }
    catch { $failed += "Files: $($_.Exception.Message)" }
}

# Remove the Settings > Apps entry last, and only if everything else is gone,
# so a failed uninstall can still be retried from there
if (-not $failed) {
    Step 'Removing Settings > Apps entry'
    try { Remove-Item $ArpKey -Recurse -Force -ErrorAction Stop }
    catch { if (Test-Path $ArpKey) { $failed += "Registry: $($_.Exception.Message)" } }
}

$pawnUninstaller = Join-Path $env:ProgramFiles 'PawnIO\uninstall.exe'
if (-not $failed -and -not $Quiet -and -not $RemovePawnIO -and (Test-Path $pawnUninstaller)) {
    $ans = Read-Host 'Also remove the PawnIO driver? Other hardware tools may need it. (y/N)'
    $RemovePawnIO = $ans -match '^[yY]'
}
if (-not $failed -and $RemovePawnIO -and (Test-Path $pawnUninstaller)) {
    Step 'Launching PawnIO uninstaller'
    Start-Process $pawnUninstaller -Wait
}

Write-Host ''
if ($failed) {
    Write-Host 'Uninstall did not complete:' -ForegroundColor Red
    $failed | ForEach-Object { Write-Host "  - $_" -ForegroundColor Red }
    Write-Host 'Close anything using the CpuTemp folder and run the uninstaller again.'
} else {
    Write-Host 'CpuTemp has been removed.' -ForegroundColor Green
}
if (-not $Quiet) { Read-Host 'Press Enter to close' | Out-Null }
if ($failed) { exit 1 }
