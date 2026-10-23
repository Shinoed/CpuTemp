; CpuTemp setup script for Inno Setup
; Build: build_setup.bat  ->  dist\CpuTemp_Setup_<version>.exe

#define AppName    "CpuTemp"
#define AppVersion "1.1.0"
#define AppExe     "CpuTempNative.exe"
#define Publisher  "Shinoed"

[Setup]
AppId={#AppName}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#Publisher}
AppPublisherURL=https://github.com/Shinoed/CpuTemp
VersionInfoVersion={#AppVersion}
SetupIconFile=cputemp.ico
DefaultDirName={autopf}\{#AppName}
DisableProgramGroupPage=yes
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
OutputDir=dist
OutputBaseFilename={#AppName}_Setup_{#AppVersion}
Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
; No Inno uninstaller: the unsigned unins000.exe is blocked by Smart App Control.
; uninstall.ps1 (run by the signed powershell.exe) is registered in Settings > Apps instead.
Uninstallable=no

[Languages]
Name: "zh_tw"; MessagesFile: "compiler:Languages\ChineseTraditional.isl"
Name: "en";    MessagesFile: "compiler:Default.isl"

[CustomMessages]
zh_tw.IntelOnly=此工具目前只支援 Intel CPU。
en.IntelOnly=This tool currently supports Intel CPUs only.
zh_tw.InstallingPawnIO=正在背景安裝 PawnIO 驅動程式...
en.InstallingPawnIO=Installing the PawnIO driver in the background...
zh_tw.PawnIOMissing=PawnIO 驅動程式沒有安裝完成，CpuTemp 將無法讀取溫度。%n請重新執行安裝程式。
en.PawnIOMissing=The PawnIO driver was not installed, so CpuTemp cannot read temperatures.%nPlease run setup again.
zh_tw.PawnIONotRunning=PawnIO 驅動程式已安裝但沒有在執行。請重新開機後再執行一次安裝程式。
en.PawnIONotRunning=The PawnIO driver is installed but not running. Please reboot and run setup again.
zh_tw.TaskFailed=無法建立開機自動啟動的排程工作。
en.TaskFailed=Could not register the startup task.

zh_tw.AlreadyInstalled=CpuTemp 已經安裝在這台電腦上
en.AlreadyInstalled=CpuTemp is already installed
zh_tw.ChooseAction=請選擇要執行的動作：
en.ChooseAction=What would you like to do?
zh_tw.ActionReinstall=重新安裝 / 更新
en.ActionReinstall=Reinstall / update
zh_tw.ActionUninstall=解除安裝
en.ActionUninstall=Uninstall

zh_tw.ReqTitle=系統需求檢查
en.ReqTitle=System requirements
zh_tw.ReqSubtitle=安裝程式已檢查這台電腦需要的元件。
en.ReqSubtitle=Setup has checked the components this computer needs.
zh_tw.ReqIntro=缺少的元件會在安裝時自動安裝。按「下一步」繼續。
en.ReqIntro=Missing components will be installed automatically. Click Next to continue.
zh_tw.ReqOS=[正常] 作業系統：%1 (64 位元)
en.ReqOS=[OK] Operating system: %1 (64-bit)
zh_tw.ReqCPU=[正常] 處理器：%1 (Intel)
en.ReqCPU=[OK] Processor: %1 (Intel)
zh_tw.ReqPawnOK=[正常] PawnIO 驅動程式：已安裝 (版本 %1)
en.ReqPawnOK=[OK] PawnIO driver: installed (version %1)
zh_tw.ReqPawnMissing=[需安裝] PawnIO 驅動程式：未安裝或安裝不完整，安裝時會自動安裝內附的 2.2.0 版
en.ReqPawnMissing=[INSTALL] PawnIO driver: missing or incomplete, the bundled 2.2.0 will be installed
zh_tw.ReqRuntime=[正常] 執行環境：只使用 Windows 內建元件，不需要 .NET 或 Visual C++ 執行階段
en.ReqRuntime=[OK] Runtime: uses only built-in Windows components, no .NET or Visual C++ runtime needed
zh_tw.ReadyPawnIO=安裝 PawnIO 驅動程式 2.2.0 (背景安裝)
en.ReadyPawnIO=Install PawnIO driver 2.2.0 (in the background)
zh_tw.ReadyTask=設定登入 Windows 時自動啟動
en.ReadyTask=Start automatically when you sign in to Windows

[Files]
Source: "{#AppExe}";             DestDir: "{app}"; Flags: ignoreversion
Source: "task.ps1";              DestDir: "{app}"; Flags: ignoreversion
Source: "uninstall.ps1";         DestDir: "{app}"; Flags: ignoreversion
Source: "IntelMSR.LICENSE.txt";  DestDir: "{app}"; Flags: ignoreversion
Source: "PawnIO_setup.exe";      Flags: dontcopy

[InstallDelete]
; Leftovers from earlier versions (script installer, Inno uninstaller)
Type: files; Name: "{app}\common.ps1"
Type: files; Name: "{app}\unins000.exe"
Type: files; Name: "{app}\unins000.dat"

#define ArpKey "Software\Microsoft\Windows\CurrentVersion\Uninstall\" + AppName

[Registry]
; Entry from 1.0.0-1.0.5, which used the Inno uninstaller
Root: HKLM; Subkey: "{#ArpKey}_is1"; Flags: deletekey
; Settings > Apps entry pointing at uninstall.ps1
Root: HKLM; Subkey: "{#ArpKey}"; Flags: deletekey
Root: HKLM; Subkey: "{#ArpKey}"; ValueType: string; ValueName: "DisplayName";          ValueData: "{#AppName} (CPU 溫度工作列工具)"
Root: HKLM; Subkey: "{#ArpKey}"; ValueType: string; ValueName: "DisplayVersion";       ValueData: "{#AppVersion}"
Root: HKLM; Subkey: "{#ArpKey}"; ValueType: string; ValueName: "Publisher";            ValueData: "{#Publisher}"
Root: HKLM; Subkey: "{#ArpKey}"; ValueType: string; ValueName: "DisplayIcon";          ValueData: "{app}\{#AppExe}"
Root: HKLM; Subkey: "{#ArpKey}"; ValueType: string; ValueName: "InstallLocation";      ValueData: "{app}"
Root: HKLM; Subkey: "{#ArpKey}"; ValueType: string; ValueName: "UninstallString";      ValueData: "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\uninstall.ps1"""
Root: HKLM; Subkey: "{#ArpKey}"; ValueType: string; ValueName: "QuietUninstallString"; ValueData: "powershell.exe -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File ""{app}\uninstall.ps1"" -Quiet"
Root: HKLM; Subkey: "{#ArpKey}"; ValueType: dword;  ValueName: "NoModify";             ValueData: 1
Root: HKLM; Subkey: "{#ArpKey}"; ValueType: dword;  ValueName: "NoRepair";             ValueData: 1
Root: HKLM; Subkey: "{#ArpKey}"; ValueType: dword;  ValueName: "EstimatedSize";        ValueData: 64

[Code]
const
  WM_CLOSE = $0010;

function IsIntel: Boolean;
var
  Vendor: String;
begin
  Result := RegQueryStringValue(HKLM, 'HARDWARE\DESCRIPTION\System\CentralProcessor\0',
    'VendorIdentifier', Vendor) and (Vendor = 'GenuineIntel');
end;

function PawnIOInstalled: Boolean;
begin
  { Both the driver service and the DLL CpuTemp loads must be present; right after a
    PawnIO uninstall the service key can linger until reboot while the files are gone }
  { Also require PawnIO's own Settings > Apps entry: a failed PawnIO uninstall can leave
    the driver and files without it, and only a reinstall repairs that state }
  Result := RegKeyExists(HKLM, 'SYSTEM\CurrentControlSet\Services\PawnIO') and
            FileExists(ExpandConstant('{commonpf64}\PawnIO\PawnIOLib.dll')) and
            RegKeyExists(HKLM64, 'Software\Microsoft\Windows\CurrentVersion\Uninstall\PawnIO');
end;

function PowerShell: String;
begin
  // sysnative: always the 64-bit PowerShell, even before 64-bit install mode is active
  // (InitializeSetup); the 32-bit one would look in Program Files (x86)
  Result := ExpandConstant('{sysnative}\WindowsPowerShell\v1.0\powershell.exe');
end;

{ Close the tray app gracefully so it removes its icon; force-kill only as a fallback }
procedure StopApp;
var
  Wnd: HWND;
  I, RC: Integer;
begin
  Wnd := FindWindowByClassName('CpuTempTray');
  if Wnd <> 0 then
  begin
    PostMessage(Wnd, WM_CLOSE, 0, 0);
    for I := 1 to 30 do
    begin
      if FindWindowByClassName('CpuTempTray') = 0 then Break;
      Sleep(100);
    end;
  end;
  Exec(ExpandConstant('{sys}\taskkill.exe'), '/F /IM {#AppExe}', '', SW_HIDE, ewWaitUntilTerminated, RC);
  if RC = 0 then Sleep(1000); { killed: give Windows time to release the exe }
end;

function BuildRequirementsReport: String;
var
  Cpu, Ver: String;
begin
  if not RegQueryStringValue(HKLM, 'HARDWARE\DESCRIPTION\System\CentralProcessor\0',
       'ProcessorNameString', Cpu) then Cpu := 'Intel';
  Result := FmtMessage(CustomMessage('ReqOS'), [GetWindowsVersionString]) + #13#10 +
            FmtMessage(CustomMessage('ReqCPU'), [Trim(Cpu)]) + #13#10;
  if PawnIOInstalled then
  begin
    if not GetVersionNumbersString(ExpandConstant('{commonpf64}\PawnIO\PawnIOLib.dll'), Ver) then
      Ver := '?';
    Result := Result + FmtMessage(CustomMessage('ReqPawnOK'), [Ver]);
  end else
    Result := Result + CustomMessage('ReqPawnMissing');
  Result := Result + #13#10 + CustomMessage('ReqRuntime');
end;

procedure InitializeWizard;
begin
  CreateOutputMsgMemoPage(wpWelcome, CustomMessage('ReqTitle'), CustomMessage('ReqSubtitle'),
    CustomMessage('ReqIntro'), BuildRequirementsReport);
end;

{ "Ready to install" page: also list the extra actions setup will perform }
function UpdateReadyMemo(Space, NewLine, MemoUserInfoInfo, MemoDirInfo, MemoTypeInfo,
  MemoComponentsInfo, MemoGroupInfo, MemoTasksInfo: String): String;
begin
  Result := MemoDirInfo + NewLine + NewLine;
  if not PawnIOInstalled then
    Result := Result + Space + CustomMessage('ReadyPawnIO') + NewLine;
  Result := Result + Space + CustomMessage('ReadyTask');
end;

{ Current (script) or 1.0.0-1.0.5 (Inno uninstaller) Settings > Apps entry }
function IsInstalled: Boolean;
begin
  Result := RegKeyExists(HKLM64, '{#ArpKey}') or RegKeyExists(HKLM64, '{#ArpKey}_is1');
end;

{ Already installed: let the same setup exe reinstall/update or uninstall.
  Uninstall runs the bundled uninstall.ps1, so it also works for installs whose
  own uninstaller is missing or blocked }
function InitializeSetup: Boolean;
var
  RC: Integer;
begin
  { /SkipCpuCheck=1 is for the CI smoke test, whose runners may have AMD CPUs }
  Result := IsIntel or (ExpandConstant('{param:SkipCpuCheck|0}') = '1');
  if not Result then
  begin
    SuppressibleMsgBox(CustomMessage('IntelOnly'), mbCriticalError, MB_OK, IDOK);
    Exit;
  end;

  if not IsInstalled or WizardSilent then Exit;

  case TaskDialogMsgBox(CustomMessage('AlreadyInstalled'), CustomMessage('ChooseAction'),
         mbConfirmation, MB_YESNOCANCEL, [CustomMessage('ActionReinstall'),
         CustomMessage('ActionUninstall')], 0) of
    IDYES: Result := True;
    IDNO:
      begin
        ExtractTemporaryFile('uninstall.ps1');
        Exec(PowerShell, '-NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "' +
          ExpandConstant('{tmp}\uninstall.ps1') + '" -NoConfirm', '', SW_HIDE, ewWaitUntilTerminated, RC);
        Result := False;
      end;
  else
    Result := False;
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  RC: Integer;
begin
  if CurStep = ssInstall then
    StopApp;

  if CurStep = ssPostInstall then
  begin
    if not PawnIOInstalled then
    begin
      WizardForm.StatusLabel.Caption := CustomMessage('InstallingPawnIO');
      ExtractTemporaryFile('PawnIO_setup.exe');
      { Silent install; if that did not work, fall back to PawnIO's own window }
      Exec(ExpandConstant('{tmp}\PawnIO_setup.exe'), '-install -silent', '', SW_HIDE, ewWaitUntilTerminated, RC);
      { No window in a silent install: nobody is there to click through it }
      if not PawnIOInstalled and not WizardSilent then
        Exec(ExpandConstant('{tmp}\PawnIO_setup.exe'), '', '', SW_SHOW, ewWaitUntilTerminated, RC);
      if not PawnIOInstalled then
      begin
        SuppressibleMsgBox(CustomMessage('PawnIOMissing'), mbError, MB_OK, IDOK);
        Exit;
      end;
    end;

    Exec(PowerShell, '-NoProfile -ExecutionPolicy Bypass -File "' + ExpandConstant('{app}\task.ps1') +
      '" -Exe "' + ExpandConstant('{app}\{#AppExe}') + '"', '', SW_HIDE, ewWaitUntilTerminated, RC);
    if RC = 2 then
      SuppressibleMsgBox(CustomMessage('PawnIONotRunning'), mbError, MB_OK, IDOK)
    else if RC <> 0 then
      SuppressibleMsgBox(CustomMessage('TaskFailed') + ' (' + IntToStr(RC) + ')', mbError, MB_OK, IDOK);
  end;
end;
