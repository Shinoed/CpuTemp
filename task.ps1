# Helper run by the setup/uninstaller (elevated) to manage the startup task.
#   task.ps1 -Exe <path>   make sure PawnIO is running, register logon task, start it
#   task.ps1 -Uninstall    remove the logon task
# Exit codes: 0 ok, 2 PawnIO not running, 3 task registration failed
param([string]$Exe, [switch]$Uninstall)

$Task = 'CpuTempTray'

if ($Uninstall) {
    Unregister-ScheduledTask -TaskName $Task -Confirm:$false -ErrorAction SilentlyContinue
    exit 0
}

try {
    $svc = Get-Service PawnIO -ErrorAction Stop
    if ($svc.Status -ne 'Running') {
        Start-Service PawnIO -ErrorAction Stop
        $svc.WaitForStatus('Running', [TimeSpan]::FromSeconds(10))
    }
} catch { exit 2 }

try {
    # The user signed in at the console, not the elevated account (they differ when a
    # standard user approves UAC with an administrator's password)
    $user = (Get-CimInstance Win32_ComputerSystem).UserName
    if (-not $user) { $user = "$env:USERDOMAIN\$env:USERNAME" }
    $action    = New-ScheduledTaskAction -Execute $Exe
    $trigger   = New-ScheduledTaskTrigger -AtLogOn -User $user
    $principal = New-ScheduledTaskPrincipal -UserId $user -LogonType Interactive -RunLevel Highest
    $settings  = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries `
                     -ExecutionTimeLimit ([TimeSpan]::Zero) -MultipleInstances IgnoreNew
    Register-ScheduledTask -TaskName $Task -Action $action -Trigger $trigger -Principal $principal `
        -Settings $settings -Description 'CPU temperature tray icon' -Force -ErrorAction Stop | Out-Null
    Start-ScheduledTask -TaskName $Task -ErrorAction Stop
} catch { exit 3 }
exit 0
