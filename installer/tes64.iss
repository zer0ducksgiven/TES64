; TES64 installer (Inno Setup 6). Built by package.sh into dist\TES64-Setup.exe.
;
;  - asks for the player's own Super Mario 64 (US) ROM and checks it (size, byte order, title, region); nothing from
;    the game ships with TES64, the plugin reads Mario, his textures and sounds from it
;  - finds Skyrim Special Edition (Bethesda's registry key, then Steam), or lets the player pick it
;  - installs the SKSE plugin into Data\SKSE\Plugins, and copies the ROM to %LOCALAPPDATA%\TES64
;  - warns if SKSE or Address Library aren't installed (both needed). A package.sh build with those sources on hand
;    (the SKSE .7z in the repo root, Address Library bins in the installed Skyrim) bundles them as a private TEST build:
;    neither mod's licence allows redistributing them, so don't publish that installer

#define AppName "TES64"
#ifndef AppVersion
  #define AppVersion "0.0.0-dev"
#endif
#define Dist "..\dist"

[Setup]
AppId={{B3C1A0F4-64D2-4E7A-9C15-7E5164A0E064}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher=TES64
DefaultDirName={code:GetSkyrimDir}
DisableDirPage=no
DirExistsWarning=no
AppendDefaultDirName=no
DisableProgramGroupPage=yes
PrivilegesRequired=admin
OutputDir={#Dist}
OutputBaseFilename=TES64-Setup
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName={#AppName} (Mario for Skyrim)

[Messages]
WelcomeLabel2=This will install TES64, which lets you play as Mario in Skyrim Special Edition (press F10 in game).%n%nYou'll need your own copy of Super Mario 64 (US version) as a ROM file. TES64 doesn't include anything from the game: Mario, his animations, textures and sounds are all read from your ROM.%n%nPlease close Skyrim before continuing.
SelectDirLabel3=Choose your Skyrim Special Edition folder (the one with SkyrimSE.exe in it).

[Files]
#ifdef Bundle
; test bundle (package.sh): SKSE64 and Address Library, never over a copy already there
Source: "{#Dist}\TES64\redist\skse\*.dll"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#Dist}\TES64\redist\skse\skse64_loader.exe"; DestDir: "{app}"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#Dist}\TES64\redist\skse\Scripts\*"; DestDir: "{app}\Data\Scripts"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#Dist}\TES64\redist\addresslib\*"; DestDir: "{app}\Data\SKSE\Plugins"; Flags: onlyifdoesntexist uninsneveruninstall
#endif
Source: "{#Dist}\TES64\Data\SKSE\Plugins\TES64.dll"; DestDir: "{app}\Data\SKSE\Plugins"; Flags: ignoreversion
Source: "{#Dist}\TES64\Data\SKSE\Plugins\TES64\sm64.dll"; DestDir: "{app}\Data\SKSE\Plugins\TES64"; Flags: ignoreversion
Source: "{#Dist}\TES64\Data\SKSE\Plugins\TES64.ini"; DestDir: "{app}\Data\SKSE\Plugins"; Flags: onlyifdoesntexist uninsneveruninstall
Source: "{#Dist}\TES64\Data\meshes\TES64\*"; DestDir: "{app}\Data\meshes\TES64"; Flags: ignoreversion
Source: "{#Dist}\TES64\Data\textures\TES64\*"; DestDir: "{app}\Data\textures\TES64"; Flags: ignoreversion
; SM64's font for Skyrim's text is made at install from the player's own ROM by this helper (nothing of the font ships)
Source: "{#Dist}\TES64\Data\SKSE\Plugins\TES64\tes64_fontgen.exe"; DestDir: "{app}\Data\SKSE\Plugins\TES64"; Flags: ignoreversion
Source: "{#Dist}\TES64\Data\TES64.esp"; DestDir: "{app}\Data"; Flags: ignoreversion
Source: "{#Dist}\TES64\Data\Scripts\*"; DestDir: "{app}\Data\Scripts"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{localappdata}\TES64"; Flags: ignoreversion

[INI]
Filename: "{app}\Data\SKSE\Plugins\TES64.ini"; Section: "General"; Key: "sRomPath"; String: "{code:GetRomDest}"

[InstallDelete]
; the old MCM Helper page (TES64 has its own SkyUI page now)
Type: filesandordirs; Name: "{app}\Data\MCM\Config\TES64"

[UninstallDelete]
Type: filesandordirs; Name: "{app}\Data\SKSE\Plugins\TES64"
; mario.dds is written by the plugin from the ROM at runtime
Type: filesandordirs; Name: "{app}\Data\textures\TES64"
Type: filesandordirs; Name: "{app}\Data\meshes\TES64"
Type: filesandordirs; Name: "{localappdata}\TES64"
Type: files; Name: "{app}\Data\TES64.esp"
Type: files; Name: "{app}\Data\Interface\fonts_tes64.swf"
Type: files; Name: "{app}\Data\Interface\fontconfig.txt"
Type: files; Name: "{app}\Data\Scripts\TES64_MCM.pex"
Type: files; Name: "{app}\Data\Scripts\TES64_Native.pex"
Type: files; Name: "{app}\Data\MCM\Settings\TES64.ini"

[Code]
const
  ROM_SIZE = 8388608;
#ifdef Bundle
  HaveBundle = True;
#else
  HaveBundle = False;
#endif

var
  RomPage: TInputFileWizardPage;
  RomExt: String;

// ---- Skyrim ----------------------------------------------------------------------------------------------------

// Steam library folders, from libraryfolders.vdf ("path" lines)
function FindInSteamLibraries(const Steam: String): String;
var
  Lines: TArrayOfString;
  I, P: Integer;
  Line, Lib: String;
begin
  Result := '';
  if FileExists(Steam + '\steamapps\common\Skyrim Special Edition\SkyrimSE.exe') then
  begin
    Result := Steam + '\steamapps\common\Skyrim Special Edition';
    exit;
  end;
  if not LoadStringsFromFile(Steam + '\steamapps\libraryfolders.vdf', Lines) then exit;
  for I := 0 to GetArrayLength(Lines) - 1 do
  begin
    Line := Trim(Lines[I]);
    if Pos('"path"', Line) = 1 then
    begin
      Lib := Trim(Copy(Line, 7, Length(Line)));
      P := Pos('"', Lib); Delete(Lib, 1, P);
      P := Pos('"', Lib); Lib := Copy(Lib, 1, P - 1);
      StringChangeEx(Lib, '\\', '\', True);
      if FileExists(Lib + '\steamapps\common\Skyrim Special Edition\SkyrimSE.exe') then
      begin
        Result := Lib + '\steamapps\common\Skyrim Special Edition';
        exit;
      end;
    end;
  end;
end;

function GetSkyrimDir(Param: String): String;
var
  Dir, Steam: String;
begin
  Result := ExpandConstant('{param:SKYRIM|}');
  if Result <> '' then exit;
  if RegQueryStringValue(HKLM32, 'SOFTWARE\Bethesda Softworks\Skyrim Special Edition', 'Installed Path', Dir) and
     FileExists(AddBackslash(Dir) + 'SkyrimSE.exe') then
  begin
    Result := RemoveBackslashUnlessRoot(Dir);
    exit;
  end;
  if RegQueryStringValue(HKCU, 'Software\Valve\Steam', 'SteamPath', Steam) then
  begin
    StringChangeEx(Steam, '/', '\', True);
    Result := FindInSteamLibraries(Steam);
  end;
  if Result = '' then
    Result := ExpandConstant('{commonpf32}\Steam\steamapps\common\Skyrim Special Edition');
end;

// ---- the ROM ---------------------------------------------------------------------------------------------------

function RomByte(const Data: AnsiString; I: Integer): Integer;
begin
  if RomExt = '.v64' then I := I xor 1
  else if RomExt = '.n64' then I := I xor 3;
  Result := Ord(Data[I + 1]);
end;

// '' if the file is a Super Mario 64 (US) ROM, otherwise what's wrong with it
function CheckRom(const FileName: String): String;
var
  Data: AnsiString;
  Title: String;
  I: Integer;
begin
  Result := '';
  if not FileExists(FileName) then begin Result := 'That file doesn''t exist.'; exit; end;
  if not LoadStringFromFile(FileName, Data) then begin Result := 'That file couldn''t be read.'; exit; end;
  if Length(Data) <> ROM_SIZE then begin Result := 'That isn''t a Super Mario 64 ROM (it should be exactly 8 MB).'; exit; end;
  if (Ord(Data[1]) = $80) and (Ord(Data[2]) = $37) and (Ord(Data[3]) = $12) and (Ord(Data[4]) = $40) then RomExt := '.z64'
  else if (Ord(Data[1]) = $37) and (Ord(Data[2]) = $80) and (Ord(Data[3]) = $40) and (Ord(Data[4]) = $12) then RomExt := '.v64'
  else if (Ord(Data[1]) = $40) and (Ord(Data[2]) = $12) and (Ord(Data[3]) = $37) and (Ord(Data[4]) = $80) then RomExt := '.n64'
  else begin Result := 'That isn''t an N64 ROM.'; exit; end;
  Title := '';
  for I := $20 to $2D do Title := Title + Chr(RomByte(Data, I));
  if Title <> 'SUPER MARIO 64' then begin Result := 'That''s an N64 ROM, but not Super Mario 64.'; exit; end;
  if RomByte(Data, $3E) <> Ord('E') then
  begin
    Result := 'That''s Super Mario 64, but not the US version. TES64 needs the US (NTSC-U) ROM.';
    exit;
  end;
end;

function GetRomDest(Param: String): String;
begin
  Result := ExpandConstant('{localappdata}\TES64\sm64.us') + RomExt;
end;

// ---- load order ------------------------------------------------------------------------------------------------

function PluginsTxt(): String;
begin
  Result := ExpandConstant('{localappdata}') + '\Skyrim Special Edition\Plugins.txt';
end;

// TES64.esp (the MCM quest, ESL-flagged) enabled in Skyrim's load order, or taken out of it
procedure SetPluginEnabled(Enable: Boolean);
var
  Lines: TArrayOfString;
  Kept: TStringList;
  I: Integer;
  Name: String;
begin
  Kept := TStringList.Create;
  try
    if LoadStringsFromFile(PluginsTxt(), Lines) then
      for I := 0 to GetArrayLength(Lines) - 1 do
      begin
        Name := Lowercase(Trim(Lines[I]));
        if (Name <> 'tes64.esp') and (Name <> '*tes64.esp') then Kept.Add(Lines[I]);
      end;
    if Enable then Kept.Add('*TES64.esp');
    ForceDirectories(ExtractFileDir(PluginsTxt()));
    Kept.SaveToFile(PluginsTxt());
  finally
    Kept.Free;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usUninstall then SetPluginEnabled(False);
end;

// ---- wizard ----------------------------------------------------------------------------------------------------

procedure InitializeWizard();
begin
  RomPage := CreateInputFilePage(wpWelcome,
    'Your Super Mario 64 ROM', 'TES64 reads Mario from your own copy of the game.',
    'Choose your Super Mario 64 (US) ROM file. Any file name is fine; .z64, .v64 and .n64 dumps all work.');
  RomPage.Add('ROM file:', 'N64 ROMs (*.z64;*.v64;*.n64)|*.z64;*.v64;*.n64|All files|*.*', '.z64');
  RomPage.Values[0] := ExpandConstant('{param:ROM|}');
end;

function NextButtonClick(CurPageID: Integer): Boolean;
var
  Problem: String;
begin
  Result := True;
  if WizardSilent then exit;
  if CurPageID = RomPage.ID then
  begin
    Problem := CheckRom(RomPage.Values[0]);
    if Problem <> '' then begin MsgBox(Problem, mbError, MB_OK); Result := False; end;
  end
  else if CurPageID = wpSelectDir then
  begin
    if not FileExists(AddBackslash(WizardDirValue) + 'SkyrimSE.exe') then
    begin
      MsgBox('SkyrimSE.exe isn''t in that folder. Choose your Skyrim Special Edition folder.', mbError, MB_OK);
      Result := False;
    end
    else if not HaveBundle and not FileExists(AddBackslash(WizardDirValue) + 'skse64_loader.exe') then
      SuppressibleMsgBox('SKSE64 doesn''t seem to be installed in this folder. TES64 needs it: get it from skse.silverlock.org, and start Skyrim with skse64_loader.exe.', mbInformation, MB_OK, IDOK);
  end;
end;

function PrepareToInstall(var NeedsRestart: Boolean): String;
begin
  Result := CheckRom(RomPage.Values[0]);
  if Result <> '' then Result := 'Super Mario 64 ROM: ' + Result;
end;

procedure CurStepChanged(CurStep: TSetupStep);
var
  Dest: String;
  FontResult: Integer;
begin
  if CurStep = ssInstall then
    ForceDirectories(ExpandConstant('{localappdata}\TES64'));
  if CurStep = ssPostInstall then
  begin
    SetPluginEnabled(True);
    Dest := GetRomDest('');
    if not FileCopy(RomPage.Values[0], Dest, False) then
      SuppressibleMsgBox('Your ROM couldn''t be copied to ' + Dest + '. Copy it there yourself, or run the installer again.', mbError, MB_OK, IDOK);
    // Skyrim's text in Super Mario 64's own dialogue font, read out of the ROM. If this fails the plugin tries again when
    // the game starts (and Skyrim keeps its own fonts if it can't).
    ForceDirectories(ExpandConstant('{app}\Data\Interface'));
    if (not Exec(ExpandConstant('{app}\Data\SKSE\Plugins\TES64\tes64_fontgen.exe'), '"' + Dest + '" "' + ExpandConstant('{app}\Data\Interface\fonts_tes64.swf') + '"', '', SW_HIDE, ewWaitUntilTerminated, FontResult)) or (FontResult <> 0) then
      SuppressibleMsgBox('The Super Mario 64 font couldn''t be made from your ROM, so Skyrim will keep its own fonts.', mbInformation, MB_OK, IDOK);
    if not FileExists(ExpandConstant('{app}\Data\SKSE\Plugins\versionlib-1-7-104-0.bin')) and
       not FileExists(ExpandConstant('{app}\Data\SKSE\Plugins\versionlib-1-6-1170-0.bin')) and
       not FileExists(ExpandConstant('{app}\Data\SKSE\Plugins\version-1-5-97-0.bin')) then
      SuppressibleMsgBox('Address Library for SKSE Plugins doesn''t seem to be installed. TES64 needs it: install it from Nexus Mods (mod 32444).', mbInformation, MB_OK, IDOK);
  end;
end;
