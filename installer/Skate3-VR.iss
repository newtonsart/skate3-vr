#ifndef PayloadDir
  #error Define PayloadDir with the clean, allowlisted package directory.
#endif
#ifndef PackageVersion
  #define PackageVersion "0.2.0-alpha"
#endif

[Setup]
AppId={{D6E24049-5BB1-4EA7-8176-59C97B0A744B}
AppName=Skate 3 VR
AppVersion={#PackageVersion}
AppPublisher=Skate3-VR contributors
AppPublisherURL=https://github.com/newtonsart/skate3-vr
DefaultDirName={localappdata}\Programs\Skate3-VR
DefaultGroupName=Skate 3 VR
DisableProgramGroupPage=no
AllowNoIcons=yes
PrivilegesRequired=lowest
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
MinVersion=10.0
WizardStyle=modern
OutputBaseFilename=Skate3-VR-Setup-{#PackageVersion}
Compression=lzma2
SolidCompression=yes
UninstallDisplayIcon={app}\bin\skate3.exe

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; Flags: unchecked

[Files]
Source: "{#PayloadDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Skate 3 VR"; Filename: "{app}\Skate3-VR.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\bin\skate3.exe"
Name: "{userdesktop}\Skate 3 VR"; Filename: "{app}\Skate3-VR.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\bin\skate3.exe"; Tasks: desktopicon

[Run]
Filename: "{app}\Skate3-VR.cmd"; Description: "Open Skate 3 VR"; Flags: shellexec nowait postinstall skipifsilent

[UninstallDelete]
Type: files; Name: "{app}\install.ini"

[Code]
var
  DataPage: TInputDirWizardPage;
  IsoPage: TInputFileWizardPage;
  GameReady: Boolean;

function HasGame(const Folder: String): Boolean;
begin
  Result := not FileExists(AddBackslash(Folder) + '.skate3-vr-installing') and
    FileExists(AddBackslash(Folder) + 'default.xex') and
    FileExists(AddBackslash(Folder) + 'data\webkit\EAWebkit.xex');
end;

function IsGameReady(const Folder: String): Boolean;
begin
  Result := False;
  if not HasGame(Folder) then Exit;
  if not FileExists(AddBackslash(Folder) + 'default.xexp') then Exit;
  if not FileExists(AddBackslash(Folder) + 'data\webkit\EAWebkit.xexp') then Exit;
  Result := (GetSHA256OfFile(AddBackslash(Folder) + 'default.xexp') =
    'eb9ef9109dfa6d940df2e156e7eaeda4603d2b2319ca6451f324b1c27f2b1f4c') and
    (GetSHA256OfFile(AddBackslash(Folder) + 'data\webkit\EAWebkit.xexp') =
    '5d4a308d2a6c768fc27c8b62ccb6661171dc504f8c0011a3e116cf0074e09438');
end;

function ContainsFolder(const Parent, Child: String): Boolean;
begin
  Result := Pos(Lowercase(AddBackslash(ExpandFileName(Parent))),
    Lowercase(AddBackslash(ExpandFileName(Child)))) = 1;
end;

function IsNonempty(const Folder: String): Boolean;
var FindRec: TFindRec;
begin
  Result := False;
  if FindFirst(AddBackslash(Folder) + '*', FindRec) then
    try
      repeat
        if (FindRec.Name <> '.') and (FindRec.Name <> '..') then begin
          Result := True;
          Exit;
        end;
      until not FindNext(FindRec);
    finally FindClose(FindRec); end;
end;

procedure InitializeWizard;
begin
  DataPage := CreateInputDirPage(wpSelectDir, 'Game files and saves',
    'Choose where to keep your game files and saved progress.',
    'These folders are kept when you uninstall Skate 3 VR. You can also select an existing game installation.', False, '');
  DataPage.Add('Game files folder:');
  DataPage.Add('Save profiles folder:');
  DataPage.Values[0] := GetPreviousData('GameDataRoot', ExpandConstant('{userdocs}\My Games\Skate3-VR\Game'));
  DataPage.Values[1] := GetPreviousData('UserDataRoot', ExpandConstant('{localappdata}\Skate3-VR\UserData'));
  if ExpandConstant('{param:GAMEDIR}') <> '' then DataPage.Values[0] := ExpandConstant('{param:GAMEDIR}');
  if ExpandConstant('{param:SAVEDIR}') <> '' then DataPage.Values[1] := ExpandConstant('{param:SAVEDIR}');
  IsoPage := CreateInputFilePage(DataPage.ID, 'Your Skate 3 ISO',
    'Select your own Xbox 360 copy of Skate 3.',
    'Setup extracts the game locally. The ISO is not modified or uploaded.');
  IsoPage.Add('Skate 3 ISO:', 'Xbox 360 ISO|*.iso', '.iso');
  IsoPage.Values[0] := ExpandConstant('{param:ISO}');
end;

function ShouldSkipPage(PageID: Integer): Boolean;
begin
  Result := (PageID = IsoPage.ID) and HasGame(DataPage.Values[0]);
end;

function ValidateFolders: String;
var A, G, S: String;
begin
  Result := '';
  A := WizardDirValue;
  G := DataPage.Values[0];
  S := DataPage.Values[1];
  if (G = '') or (S = '') then begin Result := 'Choose both folders.'; Exit; end;
  if ContainsFolder(A, G) or ContainsFolder(G, A) or ContainsFolder(A, S) or
    ContainsFolder(S, A) or ContainsFolder(G, S) or ContainsFolder(S, G) then begin
    Result := 'Choose separate folders for the application, game files and saves.'; Exit;
  end;
  if IsNonempty(G) and not HasGame(G) and not FileExists(AddBackslash(G) + '.skate3-vr-installing') then
    Result := 'The game folder is not empty and is not an existing installation. Choose an empty folder to avoid overwriting files.';
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var Error: String;
begin
  Result := True;
  if CurPageID = DataPage.ID then begin
    Error := ValidateFolders;
    if Error <> '' then begin MsgBox(Error, mbError, MB_OK); Result := False; end;
  end;
  if (CurPageID = IsoPage.ID) and not FileExists(IsoPage.Values[0]) then begin
    MsgBox('Select an existing Skate 3 ISO file.', mbError, MB_OK); Result := False;
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := ValidateFolders;
  if (Result = '') and not HasGame(DataPage.Values[0]) and not FileExists(IsoPage.Values[0]) then
    Result := 'Select an existing Skate 3 ISO file.';
end;

procedure RegisterPreviousData(PreviousDataKey: Integer);
begin
  SetPreviousData(PreviousDataKey, 'GameDataRoot', DataPage.Values[0]);
  SetPreviousData(PreviousDataKey, 'UserDataRoot', DataPage.Values[1]);
end;

procedure CurStepChanged(CurStep: TSetupStep);
var Lines: TArrayOfString; Params: String; ExitCode: Integer;
begin
  if CurStep <> ssPostInstall then Exit;
  DataPage.Values[0] := ExpandFileName(DataPage.Values[0]);
  DataPage.Values[1] := ExpandFileName(DataPage.Values[1]);
  SetArrayLength(Lines, 3);
  Lines[0] := '[Paths]';
  Lines[1] := 'GameDataRoot=' + ExpandFileName(DataPage.Values[0]);
  Lines[2] := 'UserDataRoot=' + ExpandFileName(DataPage.Values[1]);
  if not SaveStringsToUTF8File(ExpandConstant('{app}\install.ini'), Lines, False) then
    RaiseException('Unable to save installation paths.');
  Params := '--gpu_backend=vulkan --skate3_vr=false --skate3_setup_only=true --fullscreen=false ' +
    '--game_data_root="' + DataPage.Values[0] + '" --user_data_root="' + DataPage.Values[1] + '" ' +
    '--log_file="' + AddBackslash(DataPage.Values[1]) + 'setup.log"';
  if not HasGame(DataPage.Values[0]) then
    Params := Params + ' --skate3_install_iso="' + IsoPage.Values[0] + '"';
  WizardForm.StatusLabel.Caption := 'Preparing your game files and Title Update 3...';
  if Exec(ExpandConstant('{app}\bin\skate3.exe'), Params, ExpandConstant('{app}'), SW_SHOWNORMAL, ewWaitUntilTerminated, ExitCode) then
    GameReady := (ExitCode = 0) and IsGameReady(DataPage.Values[0]);
  if not GameReady and not WizardSilent then
    MsgBox('The application is installed, but game setup did not finish. Open Skate 3 VR and choose Install / finish setup to continue.', mbInformation, MB_OK);
end;

function GetCustomSetupExitCode: Integer;
begin
  if GameReady then Result := 0 else Result := 2;
end;
