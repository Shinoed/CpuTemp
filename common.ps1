# Shared settings and helpers for install.ps1 / uninstall.ps1 (dot-sourced).

$AppName = 'CpuTemp'
$Version = '1.0.0'
$Dir     = Join-Path $env:ProgramFiles $AppName
$Exe     = Join-Path $Dir 'CpuTempNative.exe'
$Task    = 'CpuTempTray'
$ArpKey  = "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppName"

function Step($msg) { Write-Host "==> $msg" -ForegroundColor Cyan }

function Test-Admin {
    ([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole('Administrators')
}

Add-Type -Namespace CpuTemp -Name Win32 -MemberDefinition @'
[DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern System.IntPtr FindWindowW(string cls, string title);
[DllImport("user32.dll")] public static extern bool PostMessageW(System.IntPtr h, uint msg, System.IntPtr w, System.IntPtr l);
'@

# Close the tray app gracefully (so it removes its icon), force-kill only as a fallback,
# and wait until the process is really gone so its exe is no longer locked.
function Stop-CpuTemp {
    $procs = @(Get-Process CpuTempNative -ErrorAction SilentlyContinue)
    if (-not $procs) { return }
    $hwnd = [CpuTemp.Win32]::FindWindowW('CpuTempTray', $null)
    if ($hwnd -ne [IntPtr]::Zero) { [void][CpuTemp.Win32]::PostMessageW($hwnd, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) } # WM_CLOSE
    $procs | Wait-Process -Timeout 3 -ErrorAction SilentlyContinue
    $procs = @(Get-Process CpuTempNative -ErrorAction SilentlyContinue)
    if ($procs) {
        $procs | Stop-Process -Force -ErrorAction SilentlyContinue
        $procs | Wait-Process -Timeout 10 -ErrorAction SilentlyContinue
    }
    if (Get-Process CpuTempNative -ErrorAction SilentlyContinue) { throw 'Could not stop CpuTempNative.exe.' }
}
