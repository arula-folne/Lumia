; LumiaMusicView — OBS Studio plugin installer (Windows x64)
; Build: ISCC.exe /DMyAppVersion=0.1.1 /DStageDir=... LumiaMusicView.iss

#ifndef MyAppVersion
  #define MyAppVersion "0.1.1"
#endif
#ifndef StageDir
  #define StageDir "..\dist\stage-installer"
#endif
#ifndef OutDir
  #define OutDir "..\dist"
#endif

#define MyAppName "LumiaMusicView"
#define MyAppPublisher "folne"
#define MyAppURL "https://www.folne.net/apps/lumia-music"
#define MyAppRepo "https://github.com/arula-folne/Lumia-Music"

[Setup]
AppId={{A8F3C2E1-7B4D-4E9A-9C1F-2D6E8B0A5F31}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppRepo}
AppUpdatesURL={#MyAppRepo}/releases
DefaultDirName={commonpf64}\obs-studio
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
LicenseFile={#StageDir}\LICENSE.txt
InfoBeforeFile={#StageDir}\INSTALL.txt
OutputDir={#OutDir}
OutputBaseFilename=LumiaMusicView-{#MyAppVersion}-windows-x64
SetupIconFile=
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
DirExistsWarning=no
UsePreviousAppDir=yes
UninstallDisplayName={#MyAppName} for OBS Studio
VersionInfoVersion={#MyAppVersion}.0
VersionInfoCompany={#MyAppPublisher}
VersionInfoDescription={#MyAppName} OBS Studio Plugin
VersionInfoProductName={#MyAppName}
VersionInfoCopyright=Copyright (C) folne. Licensed under GPL-3.0

[Languages]
Name: "japanese"; MessagesFile: "compiler:Languages\Japanese.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Messages]
japanese.BeveledLabel=OBS Studio 用プラグイン
english.BeveledLabel=Plugin for OBS Studio

[Files]
; DLL
Source: "{#StageDir}\obs-plugins\64bit\lumia-music-view.dll"; DestDir: "{app}\obs-plugins\64bit"; Flags: ignoreversion
; Overlay + locale
Source: "{#StageDir}\data\obs-plugins\lumia-music-view\*"; DestDir: "{app}\data\obs-plugins\lumia-music-view"; Flags: ignoreversion recursesubdirs createallsubdirs
; Docs next to OBS root (easy to find)
Source: "{#StageDir}\LICENSE.txt"; DestDir: "{app}\data\obs-plugins\lumia-music-view"; Flags: ignoreversion
Source: "{#StageDir}\INSTALL.txt"; DestDir: "{app}\data\obs-plugins\lumia-music-view"; Flags: ignoreversion
Source: "{#StageDir}\README.txt"; DestDir: "{app}\data\obs-plugins\lumia-music-view"; Flags: ignoreversion

[Icons]
Name: "{group}\{#MyAppName} について"; Filename: "{app}\data\obs-plugins\lumia-music-view\README.txt"
Name: "{group}\アンインストール {#MyAppName}"; Filename: "{uninstallexe}"
Name: "{group}\デザインエディタ (folne)"; Filename: "{#MyAppURL}"

[Run]
Filename: "{#MyAppURL}"; Description: "デザイン / Custom CSS エディタを開く"; Flags: postinstall shellexec skipifsilent unchecked

[UninstallDelete]
Type: filesandordirs; Name: "{app}\data\obs-plugins\lumia-music-view"

[Code]
function NextButtonClick(CurPageID: Integer): Boolean;
begin
  Result := True;
  if CurPageID = wpSelectDir then
  begin
    if not DirExists(ExpandConstant('{app}\obs-plugins\64bit')) then
    begin
      MsgBox(
        '選択したフォルダに OBS Studio が見つかりません。' + #13#10 +
        '通常は C:\Program Files\obs-studio です。' + #13#10 +
        'OBS のインストール先を選んでください。' + #13#10#13#10 +
        'The selected folder does not look like OBS Studio.' + #13#10 +
        'Please choose your OBS install directory.',
        mbError, MB_OK);
      Result := False;
    end;
  end;
end;
