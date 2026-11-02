; Targets Inno Setup 6.x. Release owner must pin and validate the compiler.
; ISCC.exe /DPayloadDir="C:\release\payload" /DProductVersion="0.1.0" BG3RTX.iss
; Payload contract and integration handoff: SETUP.md. Uses B1 original branding.
#ifndef PayloadDir
  #error PayloadDir must point to a verified release payload
#endif
#ifndef ProductVersion
  #error ProductVersion must be supplied by the release owner
#endif

[Setup]
AppId={{8A32895E-1307-462E-B807-BE5B8072A9F5}
AppName=BG3RTX
AppVersion={#ProductVersion}
AppPublisher=BG3RTX contributors
AppComments=Independent fan mod
DefaultDirName={localappdata}\Programs\BG3RTX
PrivilegesRequired=lowest
Uninstallable=yes
CreateUninstallRegKey=yes
UninstallDisplayIcon={app}\bin\BG3RTX.exe
UninstallFilesDir={app}\setup-support
DisableProgramGroupPage=yes
DisableDirPage=no
UsePreviousAppDir=yes
OutputBaseFilename=BG3RTX-{#ProductVersion}-Setup
OutputDir=output
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
CloseApplications=no
RestartApplications=no
SetupLogging=yes
SetupIconFile=..\..\assets\branding\bg3rtx.ico
; Release owner must supply Authenticode signing before public distribution.
; SignTool and SignedUninstaller are intentionally not fabricated here.

[Tasks]
Name: "startmenu"; Description: "Create a Start menu shortcut"; Flags: checkedonce
Name: "desktop"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "lifecycle.ps1"; DestDir: "{tmp}"; DestName: "bg3rtx-lifecycle.ps1"; Flags: dontcopy
Source: "{#PayloadDir}\*"; DestDir: "{tmp}\bg3rtx-payload"; Flags: dontcopy recursesubdirs createallsubdirs
; Recursive payload above includes bin\ui\{index.html,app.css,app.js}, fonts and
; branding. Check these mandatory inputs at compile time instead of silently
; producing an installer whose WebView UI cannot load. Lifecycle owns deployment.
#if !FileExists(PayloadDir + "\bin\ui\index.html") || !FileExists(PayloadDir + "\bin\ui\app.css") || !FileExists(PayloadDir + "\bin\ui\app.js")
  #error Missing bundled WebView UI in verified payload
#endif
#if !FileExists(PayloadDir + "\bin\ui\fonts\LatoWeb-Regular.ttf") || !FileExists(PayloadDir + "\bin\ui\fonts\LatoWeb-Semibold.ttf") || !FileExists(PayloadDir + "\bin\ui\fonts\OFL.txt")
  #error Missing bundled UI fonts or OFL licence in verified payload
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\bg3rtx.svg")
  #error Missing original launcher emblem in verified payload
#endif
Source: "lifecycle.ps1"; DestDir: "{app}\setup-support"; Flags: ignoreversion

; Approved original ornaments and provenance must accompany the local UI.
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\README.md")
  #error Missing launcher ornament README.md
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\corner.svg")
  #error Missing launcher ornament corner.svg
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\dark-stone.svg")
  #error Missing launcher ornament dark-stone.svg
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\divider.svg")
  #error Missing launcher ornament divider.svg
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\frame.svg")
  #error Missing launcher ornament frame.svg
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\glyph-help.svg")
  #error Missing launcher ornament glyph-help.svg
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\glyph-light.svg")
  #error Missing launcher ornament glyph-light.svg
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\glyph-memory.svg")
  #error Missing launcher ornament glyph-memory.svg
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\glyph-performance.svg")
  #error Missing launcher ornament glyph-performance.svg
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\glyph-water.svg")
  #error Missing launcher ornament glyph-water.svg
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\hero-lantern.png")
  #error Missing launcher ornament hero-lantern.png
#endif
#if !FileExists(PayloadDir + "\bin\ui\branding\ornaments\parchment-panel.svg")
  #error Missing launcher ornament parchment-panel.svg
#endif

[Icons]
Name: "{autoprograms}\BG3RTX"; Filename: "{app}\bin\BG3RTX.exe"; WorkingDir: "{app}\bin"; Tasks: startmenu
Name: "{autodesktop}\BG3RTX"; Filename: "{app}\bin\BG3RTX.exe"; WorkingDir: "{app}\bin"; Tasks: desktop

[Code]
var
  Applied, Completed: Boolean;
  RemoveSettings, RemoveCache: Boolean;

function Lifecycle(Action, Extra, Script: String): Boolean;
var Code: Integer;
begin
  Result := Exec(ExpandConstant('{sys}\WindowsPowerShell\v1.0\powershell.exe'),
    '-NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' + Script +
    '" -Action ' + Action + ' -Destination "' + ExpandConstant('{app}') +
    '" -Owner inno ' + Extra, '', SW_HIDE, ewWaitUntilTerminated, Code);
  if Result then Result := Code = 0;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
var Extra: String;
begin
  Result := '';
  ExtractTemporaryFiles('{tmp}\bg3rtx-payload\*');
  ExtractTemporaryFile('bg3rtx-lifecycle.ps1');
  if not Lifecycle('Inspect', '', ExpandConstant('{tmp}\bg3rtx-lifecycle.ps1')) then begin
    if MsgBox('An interrupted setup or a running app may prevent installation. Close the game and BG3RTX settings. Attempt recovery of the previous complete state before continuing?', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) <> IDYES then begin
      Result := 'Installation paused. Recovery and backups have been retained.';
      exit;
    end;
    if not Lifecycle('Recover', '', ExpandConstant('{tmp}\bg3rtx-lifecycle.ps1')) then begin
      Result := 'Recovery failed. Keep the backup folder and see SETUP.md before retrying.';
      exit;
    end;
  end;
  Extra := '-Payload "' + ExpandConstant('{tmp}\bg3rtx-payload') + '" -DeferCommit';
  if FileExists(ExpandConstant('{app}\.bg3rtx_install')) then begin
    if MsgBox('Transfer this legacy installation to Windows Setup ownership? Settings and original-file backups will be kept.', mbConfirmation, MB_YESNO) <> IDYES then begin
      Result := 'Ownership transfer was cancelled.';
      exit;
    end;
    Extra := Extra + ' -TransferLegacy';
  end;
  if not Lifecycle('Install', Extra, ExpandConstant('{tmp}\bg3rtx-lifecycle.ps1')) then
    Result := 'Setup could not stage or replace BG3RTX. Close the game and settings app, check the destination and disk space, then retry. An interrupted transaction requires recovery; see SETUP.md. Choose the default per-user folder if the location is not writable.'
  else Applied := True;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    if not Lifecycle('Commit', '', ExpandConstant('{tmp}\bg3rtx-lifecycle.ps1')) then
      RaiseException('Could not commit BG3RTX. Recovery data has been retained.');
    Completed := True;
  end;
end;

procedure DeinitializeSetup;
begin
  if Applied and not Completed then
    Lifecycle('Recover', '', ExpandConstant('{tmp}\bg3rtx-lifecycle.ps1'));
end;

function InitializeUninstall: Boolean;
var CacheSize: AnsiString;
begin
  { Check processes and ownership before prompting or Inno removes anything. }
  Result := Lifecycle('Inspect', '', ExpandConstant('{app}\setup-support\lifecycle.ps1'));
  if not Result then begin
    MsgBox('Close the game and settings app before uninstalling. If setup was interrupted, recover it first. No process was stopped.', mbError, MB_OK);
    exit;
  end;
  RemoveSettings := False;
  RemoveCache := False;
  if not UninstallSilent then begin
    RemoveSettings := MsgBox('Remove your BG3RTX settings too? Choose No to keep settings and reports (recommended). Reports are always kept.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES;
    if Lifecycle('CacheInfo', '-SummaryPath "' + ExpandConstant('{tmp}\bg3rtx-cache-size.txt') + '"', ExpandConstant('{app}\setup-support\lifecycle.ps1')) and
       LoadStringFromFile(ExpandConstant('{tmp}\bg3rtx-cache-size.txt'), CacheSize) then
      RemoveCache := MsgBox('Remove the compiled shader cache (' + String(CacheSize) + ')? It will be rebuilt if BG3RTX is installed again.', mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES
    else
      MsgBox('Cache size could not be checked safely. The shader cache will be kept.', mbInformation, MB_OK);
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var Extra: String;
begin
  if CurUninstallStep = usUninstall then begin
    Extra := '';
    if RemoveSettings then Extra := Extra + ' -RemoveSettings';
    if RemoveCache then Extra := Extra + ' -RemoveShaderCache';
    { A previous attempt may have completed our transaction before Inno itself failed.
      Inspect above still rejects a pending journal; do not delete the payload twice. }
    if FileExists(ExpandConstant('{app}\.bg3rtx-state.json')) then
      if not Lifecycle('Uninstall', Extra, ExpandConstant('{app}\setup-support\lifecycle.ps1')) then
        RaiseException('BG3RTX cleanup failed. Ownership and recovery files have been retained; retry after resolving the error.');
    if not UninstallSilent then
      MsgBox('Reports and previous-version backups were retained under Local AppData\BG3RTX. If used, clear the BG3RTX Steam launch option in game Properties > General.', mbInformation, MB_OK);
  end;
end;
