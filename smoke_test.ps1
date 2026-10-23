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
    static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32")] public static extern bool PostMessage(IntPtr w, uint msg, IntPtr wp, IntPtr lp);
    [DllImport("user32", CharSet = CharSet.Unicode)]
    static extern int GetWindowText(IntPtr w, System.Text.StringBuilder s, int n);

    // Called from C#: PowerShell would pass $null as "" and match only an empty title
    public static IntPtr Window() { return FindWindow("CpuTempTray", null); }

    // The app mirrors its temperature tooltip into its hidden window's title
    public static string Tooltip() {
        IntPtr w = Window();
        if (w == IntPtr.Zero) return null;
        var s = new System.Text.StringBuilder(256);
        GetWindowText(w, s, s.Capacity);
        return s.ToString();
    }

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
    $w = [Native]::Window()
    if ($w -eq [IntPtr]::Zero) { return $false }
    [Native]::PostMessage($w, 0x0111, [IntPtr]$id, [IntPtr]::Zero) # WM_COMMAND
}

function WaitFor([scriptblock]$cond, $sec = 10) {
    $end = (Get-Date).AddSeconds($sec)
    while ((Get-Date) -lt $end) { if (& $cond) { return $true }; Start-Sleep -Milliseconds 200 }
    & $cond
}

function Running { [bool](Get-Process CpuTempNative -ErrorAction SilentlyContinue) }
# Memory of the running app: private working set is what Task Manager's Memory column shows
function Memory($label) {
    $p = Get-Process CpuTempNative
    $pws = (Get-Counter '\Process(CpuTempNative)\Working Set - Private').CounterSamples[0].CookedValue
    $m = [pscustomobject]@{ WS = $p.WorkingSet64 / 1MB; PrivateWS = $pws / 1MB; Commit = $p.PrivateMemorySize64 / 1MB }
    Write-Host ("MEM   {0,-28} WS {1,6:N2} MB  private WS {2,6:N2} MB  commit {3,6:N2} MB" -f $label, $m.WS, $m.PrivateWS, $m.Commit)
    $m
}

function RegValue($name) { (Get-ItemProperty $Reg -Name $name -ErrorAction SilentlyContinue).$name }

# --- Built files
foreach ($f in 'CpuTempNative.exe', $Setup) {
    $leaf = Split-Path $f -Leaf
    Check "$leaf has an icon" ([Native]::IconGroups((Resolve-Path $f)) -gt 0)
    $vi = (Get-Item $f).VersionInfo
    # Inno Setup pads the setup's version strings with spaces
    Check "$leaf company is $Publisher" ($vi.CompanyName.Trim() -eq $Publisher) "(got '$($vi.CompanyName)')"
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
Check 'starts with an invalid saved interval' (WaitFor { (Running) -and [Native]::Window() -ne [IntPtr]::Zero })
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

Check 'tooltip is set' (WaitFor { [Native]::Tooltip() -like 'CPU*' }) "(got '$([Native]::Tooltip())')"
$m0 = Memory 'no fans'
Check 'private working set under 3 MB without fans' ($m0.PrivateWS -lt 3) "($([math]::Round($m0.PrivateWS, 2)) MB)"
Check 'no fan line without Dell Command | Monitor' ([Native]::Tooltip() -notmatch '風扇') "(got '$([Native]::Tooltip())')"

Command 102 | Out-Null         # "顯示風扇轉速": not in the menu without fans, must be ignored
Start-Sleep 1
Check 'fan toggle ignored without fans' ($null -eq (RegValue ShowFan)) "(got $(RegValue ShowFan))"

Command 100 | Out-Null         # "結束"
Check 'exits from the menu' (WaitFor { -not (Running) })

# --- Fan speed: fake the DCIM_NumericSensor class Dell Command | Monitor provides
$mof = Join-Path $env:TEMP 'fake_dcim.mof'
@'
#pragma namespace("\\\\.\\root")
instance of __Namespace { Name = "dcim"; };
#pragma namespace("\\\\.\\root\\dcim")
instance of __Namespace { Name = "sysman"; };
#pragma namespace("\\\\.\\root\\dcim\\sysman")
class DCIM_NumericSensor {
    [key] string DeviceID;
    string ElementName;
    uint16 SensorType;
    sint32 CurrentReading;
    sint32 UnitModifier;
};
instance of DCIM_NumericSensor { DeviceID = "Fan1"; ElementName = "Processor Fan"; SensorType = 5; CurrentReading = 2345; UnitModifier = 0; };
instance of DCIM_NumericSensor { DeviceID = "Fan2"; ElementName = "Video Fan"; SensorType = 5; CurrentReading = 180; UnitModifier = 1; };
instance of DCIM_NumericSensor { DeviceID = "Temp1"; ElementName = "CPU Temp"; SensorType = 2; CurrentReading = 45; UnitModifier = 0; };
'@ | Set-Content $mof -Encoding ascii
mofcomp.exe $mof | Out-Null
Check 'fake DCIM class compiled' ($LASTEXITCODE -eq 0)

Start-Process $Exe
Check 'fan speeds in tooltip' (WaitFor { [Native]::Tooltip() -match '風扇 (2345 / 1800|1800 / 2345) RPM' }) "(got '$([Native]::Tooltip())')"
Check 'temperature sensor is not shown as a fan' ([Native]::Tooltip() -match '風扇 \d+ / \d+ RPM$') "(got '$([Native]::Tooltip())')"

Set-ItemProperty $Reg Interval 1 -Type DWord
Get-CimInstance -Namespace root\dcim\sysman -ClassName DCIM_NumericSensor -Filter "DeviceID='Fan1'" |
    Set-CimInstance -Property @{ CurrentReading = 3100 }
Command (200 + 1) | Out-Null
Check 'fan speed updates' (WaitFor { [Native]::Tooltip() -match '3100' }) "(got '$([Native]::Tooltip())')"

# 1 s interval: 40 WMI queries; committed memory must not keep growing
$m1 = Memory 'fans, after 5 s'
Start-Sleep 40
$m2 = Memory 'fans, after 45 s'
Check 'no memory growth over 40 fan queries' ($m2.Commit - $m1.Commit -lt 0.5) "(commit +$([math]::Round($m2.Commit - $m1.Commit, 2)) MB)"
Check 'private working set under 3 MB with fans' ($m2.PrivateWS -lt 3) "($([math]::Round($m2.PrivateWS, 2)) MB)"

Command 102 | Out-Null
Check 'fan icon off is saved' (WaitFor { (RegValue ShowFan) -eq 0 }) "(got $(RegValue ShowFan))"
Command 102 | Out-Null
Check 'fan icon on is saved' (WaitFor { (RegValue ShowFan) -eq 1 }) "(got $(RegValue ShowFan))"
Check 'fan line stays in the tooltip' ([Native]::Tooltip() -match '3100')

Command 100 | Out-Null
Check 'exits with fan reading on' (WaitFor { -not (Running) })

# --- Uninstall
& "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "$App\uninstall.ps1" -Quiet
Check 'Settings > Apps entry removed' (WaitFor { -not (Test-Path $Arp) } 30)
Check 'program folder removed' (WaitFor { -not (Test-Path $Exe) } 30)
Check 'logon task removed' (-not (Get-ScheduledTask CpuTempTray -ErrorAction SilentlyContinue))

if ($fails) { Write-Host "$fails check(s) failed"; exit 1 }
Write-Host 'all smoke tests passed'
