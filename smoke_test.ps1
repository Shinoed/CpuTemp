# Install-and-run smoke test for CI (needs an elevated, disposable Windows machine).
#   smoke_test.ps1 -Setup dist\CpuTemp_Setup_<ver>.exe -Version <ver>
# Checks the built files, installs silently, drives the running app through its
# window messages, then uninstalls. Exits non-zero on the first failed check.
param([Parameter(Mandatory)][string]$Setup, [Parameter(Mandatory)][string]$Version)

$ErrorActionPreference = 'Stop'
$Publisher = 'Shinoed'
$App = "$env:ProgramFiles\CpuTemp"
$Exe = "$App\CpuTempNative.exe"
$Arp = 'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\CpuTemp'
$Reg = 'HKCU:\Software\CpuTemp'
$fails = 0

function Check($name, [bool]$ok, $detail = '') {
    if ($ok) { Write-Host "PASS  $name" }
    else { Write-Host "FAIL  $name $detail"; $script:fails++ }
}

Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Native {
    [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
    public static extern IntPtr LoadLibraryEx(string path, IntPtr file, uint flags);
    [DllImport("kernel32")] public static extern bool FreeLibrary(IntPtr h);
    [DllImport("kernel32", CharSet = CharSet.Unicode)]
    static extern bool EnumResourceNames(IntPtr h, IntPtr type, EnumProc cb, IntPtr p);
    delegate bool EnumProc(IntPtr h, IntPtr type, IntPtr name, IntPtr p);
    [DllImport("user32", CharSet = CharSet.Unicode)]
    public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32")] public static extern bool PostMessage(IntPtr w, uint msg, IntPtr wp, IntPtr lp);

    // Number of RT_GROUP_ICON resources in a PE file
    public static int IconGroups(string path) {
        IntPtr h = LoadLibraryEx(path, IntPtr.Zero, 0x22); // DATAFILE | IMAGE_RESOURCE
        if (h == IntPtr.Zero) return -1;
        int n = 0;
        EnumResourceNames(h, (IntPtr)14, (a, b, c, d) => { n++; return true; }, IntPtr.Zero);
        FreeLibrary(h);
        return n;
    }
}
'@

function Command($id) {
    $w = [Native]::FindWindow('CpuTempTray', $null)
    if ($w -eq [IntPtr]::Zero) { return $false }
    [Native]::PostMessage($w, 0x0111, [IntPtr]$id, [IntPtr]::Zero) # WM_COMMAND
}

function WaitFor([scriptblock]$cond, $sec = 10) {
    $end = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $end) { if (& $cond) { return $true }; Start-Sleep -Milliseconds 200 }
    & $cond
}

function Running { [bool](Get-Process CpuTempNative -ErrorAction SilentlyContinue) }
function RegValue($name) { (Get-ItemProperty $Reg -Name $name -ErrorAction SilentlyContinue).$name }

# --- Built files
foreach ($f in 'CpuTempNative.exe', $Setup) {
    $leaf = Split-Path $f -Leaf
    Check "$leaf has an icon" ([Native]::IconGroups((Resolve-Path $f)) -gt 0)
    $vi = (Get-Item $f).VersionInfo
    Check "$leaf company is $Publisher" ($vi.CompanyName -eq $Publisher) "(got '$($vi.CompanyName)')"
    Check "$leaf version is $Version" ($vi.ProductVersion -like "$Version*") "(got '$($vi.ProductVersion)')"
}

# --- Silent install (SkipCpuCheck: CI runners may have AMD CPUs)
$p = Start-Process $Setup '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SkipCpuCheck=1', '/LOG=setup.log' -Wait -PassThru
Check 'setup exits with 0' ($p.ExitCode -eq 0) "(got $($p.ExitCode); see setup.log)"
Check 'exe installed' (Test-Path $Exe)
$arp = Get-ItemProperty $Arp -ErrorAction SilentlyContinue
Check 'Settings > Apps entry' ([bool]$arp)
Check "Settings > Apps publisher is $Publisher" ($arp.Publisher -eq $Publisher) "(got '$($arp.Publisher)')"
Check "Settings > Apps version is $Version" ($arp.DisplayVersion -eq $Version) "(got '$($arp.DisplayVersion)')"
Check 'Settings > Apps icon is the exe' ($arp.DisplayIcon -eq $Exe) "(got '$($arp.DisplayIcon)')"
Check 'PawnIO installed' (Test-Path "$env:ProgramFiles\PawnIO\PawnIOLib.dll")
Check 'logon task registered' ([bool](Get-ScheduledTask CpuTempTray -ErrorAction SilentlyContinue))

# --- Running app. The logon task may not start without an interactive session,
# so start the exe directly with a bad saved interval
Stop-Process -Name CpuTempNative -Force -ErrorAction SilentlyContinue
New-Item $Reg -Force | Out-Null
Set-ItemProperty $Reg Interval 99 -Type DWord
Set-ItemProperty $Reg ShowUsage 1 -Type DWord
Start-Process $Exe
Check 'starts with an invalid saved interval' (WaitFor { (Running) -and [Native]::FindWindow('CpuTempTray', $null) -ne [IntPtr]::Zero })
Start-Sleep 3
Check 'still running after 3 s' (Running)

Command (200 + 5) | Out-Null   # "每 5 秒更新"
Check 'interval 5 s is saved' (WaitFor { (RegValue Interval) -eq 5 }) "(got $(RegValue Interval))"
Command 101 | Out-Null         # "顯示 CPU 使用率" off
Check 'usage icon off is saved' (WaitFor { (RegValue ShowUsage) -eq 0 }) "(got $(RegValue ShowUsage))"
Command 101 | Out-Null
Check 'usage icon on is saved' (WaitFor { (RegValue ShowUsage) -eq 1 })

Start-Process $Exe             # second instance must exit at once (single-instance mutex)
Start-Sleep 2
Check 'only one instance runs' (@(Get-Process CpuTempNative -ErrorAction SilentlyContinue).Count -eq 1)

Command 100 | Out-Null         # "結束"
Check 'exits from the menu' (WaitFor { -not (Running) })

# --- Uninstall
& "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$App\uninstall.ps1" -Quiet
Check 'Settings > Apps entry removed' (WaitFor { -not (Test-Path $Arp) } 30)
Check 'program folder removed' (WaitFor { -not (Test-Path $Exe) } 30)
Check 'logon task removed' (-not (Get-ScheduledTask CpuTempTray -ErrorAction SilentlyContinue))

if ($fails) { Write-Host "$fails check(s) failed"; exit 1 }
Write-Host 'all smoke tests passed'
