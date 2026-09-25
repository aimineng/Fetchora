; ---------------------------------------------------------------------------
;  Fetchora - Inno Setup script
;
;  Packages the staging directory the release workflow assembles: the app, the
;  Qt runtime windeployqt produced, the bundled aria2 engine, and the licence
;  texts that have to travel with those binaries.
;
;  Build it with:
;      ISCC.exe /DAppVersion=0.1 /DStageDir=..\..\stage\Fetchora fetchora.iss
;  Both defines have working defaults, so opening the file in the Inno Setup
;  Compiler IDE and pressing F9 also works.
;
;  NOTE: AppId below must never change. Inno uses it to recognise an existing
;  installation; a new value turns every upgrade into a second, parallel install.
; ---------------------------------------------------------------------------

#ifndef AppVersion
  #define AppVersion "0.1"
#endif

; The tree to package. The workflow fills it with:
;   Fetchora.exe, aria2c.exe, ARIA2-BUILD-INFO.txt, ca-bundle.crt,
;   LICENSE, README.md, THIRD-PARTY-NOTICES.md, licenses/**, and the Qt runtime
;   (Qt6*.dll plus platforms/, imageformats/, sqldrivers/, tls/, styles/, ...).
#ifndef StageDir
  #define StageDir "..\..\stage\Fetchora"
#endif

#ifndef BuildDir
  #define BuildDir "..\..\build\release-installer"
#endif

[Setup]
AppId={{492E4DB8-AD37-49B4-BDC4-751614F00570}
AppName=Fetchora
AppVersion={#AppVersion}
AppVerName=Fetchora {#AppVersion}
AppPublisher=Fetchora contributors
AppPublisherURL=https://github.com/aimineng/Fetchora
AppSupportURL=https://github.com/aimineng/Fetchora/issues
AppUpdatesURL=https://github.com/aimineng/Fetchora/releases
AppCopyright=MIT licensed. Bundles aria2 (GPL-2.0-or-later) and Qt (LGPL-3.0).

; Per-user by default: a download manager has no business asking for
; administrator rights, and {autopf} resolves to %LOCALAPPDATA%\Programs for a
; normal user. Anyone who wants a machine-wide install can still pick it in the
; dialog.
DefaultDirName={autopf}\Fetchora
DefaultGroupName=Fetchora
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
AllowNoIcons=yes

; The published build is x64-only; there is no 32-bit Qt or aria2 in the package.
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; Shown before the file list. The MIT licence is Fetchora's own; the third-party
; notice is there because the package really does contain GPL and LGPL binaries
; and the user is entitled to know that before installing, not afterwards.
LicenseFile={#StageDir}\LICENSE
InfoBeforeFile={#StageDir}\THIRD-PARTY-NOTICES.md

OutputDir={#BuildDir}
OutputBaseFilename=Fetchora-{#AppVersion}-windows-x64-setup
SetupIconFile=..\..\app.ico
WizardStyle=modern
Compression=lzma2/ultra64
SolidCompression=yes
LZMANumBlockThreads=4
DisableProgramGroupPage=yes

; Replacing files of a running instance is what an upgrade normally is, so ask
; the user to let Setup close it instead of failing on a locked DLL.
CloseApplications=yes
RestartApplications=no

UninstallDisplayName=Fetchora
UninstallDisplayIcon={app}\Fetchora.exe

[Languages]
; Inno ships English by default. A Simplified Chinese translation is available
; as an unofficial language file; drop ChineseSimplified.isl into Inno's
; Languages folder and add a matching line here to offer it.
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "assoc";       Description: "Associate Fetchora with .torrent and .metalink files"; GroupDescription: "File associations:"; Flags: unchecked

[Files]
; Everything in the staging tree, including the Qt plugin subdirectories and
; licenses/. Enumerating the Qt DLLs by hand would go stale the moment the Qt
; version changes.
Source: "{#StageDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\Fetchora"; Filename: "{app}\Fetchora.exe"
Name: "{group}\{cm:UninstallProgram,Fetchora}"; Filename: "{uninstallexe}"
Name: "{autodesktop}\Fetchora"; Filename: "{app}\Fetchora.exe"; Tasks: desktopicon

[Registry]
; Optional associations. Both hand the file to the app on the command line;
; main.cpp forwards .torrent/.metalink paths to aria2.
Root: HKA; Subkey: "Software\Classes\Fetchora.Torrent"; ValueType: string; ValueName: ""; ValueData: "BitTorrent metainfo file"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Fetchora.Torrent\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Fetchora.exe,0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Fetchora.Torrent\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Fetchora.exe"" ""%1"""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.torrent"; ValueType: string; ValueName: ""; ValueData: "Fetchora.Torrent"; Flags: uninsdeletevalue; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Fetchora.Metalink"; ValueType: string; ValueName: ""; ValueData: "Metalink file"; Flags: uninsdeletekey; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Fetchora.Metalink\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\Fetchora.exe,0"; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\Fetchora.Metalink\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\Fetchora.exe"" ""%1"""; Tasks: assoc
Root: HKA; Subkey: "Software\Classes\.metalink"; ValueType: string; ValueName: ""; ValueData: "Fetchora.Metalink"; Flags: uninsdeletevalue; Tasks: assoc

[Run]
Description: "{cm:LaunchProgram,Fetchora}"; Filename: "{app}\Fetchora.exe"; Flags: nowait postinstall skipifsilent

[Code]
// The app keeps its state outside {app}: settings in the registry under
// HKCU\Software\Fetchora\Fetchora, and the session file plus the SQLite history
// in %APPDATA%\Fetchora\Fetchora. An uninstaller that deleted those without
// asking would throw away the user's download history, so it asks - with "keep"
// as the default, because reinstalling is the common case.
procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  DataDir: String;
begin
  if CurUninstallStep = usPostUninstall then
  begin
    DataDir := ExpandConstant('{userappdata}\Fetchora');
    if MsgBox('Also delete your settings, download history and session file?' + #13#10 + #13#10 +
              DataDir + '\Fetchora' + #13#10 +
              'HKEY_CURRENT_USER\Software\Fetchora' + #13#10 + #13#10 +
              'Choose No to keep them, which is what you want if you plan to reinstall.',
              mbConfirmation, MB_YESNO or MB_DEFBUTTON2) = IDYES then
    begin
      DelTree(DataDir, True, True, True);
      RegDeleteKeyIncludingSubkeys(HKEY_CURRENT_USER, 'Software\Fetchora');
    end;
  end;
end;
