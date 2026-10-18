; Inno Setup script for the ModelViewer Windows installer.
; Non-commercial use only
;
; All paths are relative to this script's own location ({#SourcePath}) so the
; script works from any repo checkout, not just a specific machine/drive.
;
; Build locally (matches the ninja_release_vcpkg CMake preset's install output):
;   iscc packaging\windows\mvinstaller.iss
;
; Build from a different install directory (e.g. CI, where the install prefix
; is just "install/" relative to the repo root):
;   iscc /DMyAppInstallDir="..\..\install" packaging\windows\mvinstaller.iss

#define MyAppName "ModelViewer"
#define MyAppVersion "2026.11.0"
#define MyAppArch "Win-X64"
#define MyAppPublisher "Sharjith N"
#define MyAppURL "https://github.com/sharjith/ModelViewer-Qt"
#define MyAppExeName "ModelViewer.exe"
#define MyAppAssocName MyAppName + " File"
#define MyAppAssocExt ".mvf"
#define MyAppAssocKey StringChange(MyAppAssocName, " ", "") + MyAppAssocExt

; Repo root, resolved relative to this script (packaging/windows/mvinstaller.iss).
#define RepoRoot SourcePath + "..\.."

; Directory containing the built+installed app (bin\, fonts\, shaders\, data\, etc.).
; Defaults to the local ninja_release_vcpkg CMake preset's install output; override
; with /DMyAppInstallDir=... to point at a different install prefix (e.g. CI).
#ifndef MyAppInstallDir
  #define MyAppInstallDir RepoRoot + "\out\install\ninja_release_vcpkg"
#endif

[Setup]
; NOTE: The value of AppId uniquely identifies this application. Do not use the same AppId value in installers for other applications.
; (To generate a new GUID, click Tools | Generate GUID inside the IDE.)
AppId={{F768EB3E-213E-4D43-995D-9EE6A8FBED44}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
;AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
UninstallDisplayIcon={app}\{#MyAppExeName}
; "ArchitecturesAllowed=x64compatible" specifies that Setup cannot run
; on anything but x64 and Windows 11 on Arm.
ArchitecturesAllowed=x64compatible
; "ArchitecturesInstallIn64BitMode=x64compatible" requests that the
; install be done in "64-bit mode" on x64 or Windows 11 on Arm,
; meaning it should use the native 64-bit Program Files directory and
; the 64-bit view of the registry.
ArchitecturesInstallIn64BitMode=x64compatible
ChangesAssociations=yes
DisableProgramGroupPage=yes
LicenseFile={#RepoRoot}\gpl-3.0.txt
; Uncomment the following line to run in non administrative install mode (install for current user only).
;PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog
OutputDir={#RepoRoot}\dist
OutputBaseFilename={#MyAppName}-{#MyAppVersion}-{#MyAppArch}-installer
SetupIconFile={#RepoRoot}\res\ModelViewer.ico
SolidCompression=yes
WizardStyle=modern dynamic

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "french"; MessagesFile: "compiler:Languages\French.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"
Name: "italian"; MessagesFile: "compiler:Languages\Italian.isl"
Name: "spanish"; MessagesFile: "compiler:Languages\Spanish.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
Source: "{#MyAppInstallDir}\bin\{#MyAppExeName}"; DestDir: "{app}\bin"; Flags: ignoreversion
Source: "{#MyAppInstallDir}\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; NOTE: Don't use "Flags: ignoreversion" on any shared system files

[Registry]
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocExt}\OpenWithProgids"; ValueType: string; ValueName: "{#MyAppAssocKey}"; ValueData: ""; Flags: uninsdeletevalue
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}"; ValueType: string; ValueName: ""; ValueData: "{#MyAppAssocName}"; Flags: uninsdeletekey
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\DefaultIcon"; ValueType: string; ValueName: ""; ValueData: "{app}\bin\{#MyAppExeName},0"
Root: HKA; Subkey: "Software\Classes\{#MyAppAssocKey}\shell\open\command"; ValueType: string; ValueName: ""; ValueData: """{app}\bin\{#MyAppExeName}"" ""%1"""

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\bin\{#MyAppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\bin\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent
