; This file is part of the dosbox-automation Project.
; License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
;
; Inno Setup script for dosbox-automation Windows installer.
;
; Expects a staged build directory with dosbox.exe, Resources/, and docs.
; The build-windows.sh script creates this staging layout.
;
; Build with:
;   iscc dosbox-automation-installer.iss
;   iscc /DMyAppVersion="0.85.0" /DStagingDir="C:\BUILD\STAGING\dosbox-automation-0.85.0" dosbox-automation-installer.iss

#define MyAppName "dosbox-automation"
; Version can be overridden via ISCC /DMyAppVersion="x.y.z-daN"
#ifndef MyAppVersion
  #define MyAppVersion "0.85.0"
#endif
#define MyAppPublisher "dosbox-automation contributors"
#define MyAppURL "https://dosbox-automation.org"
#define MyAppExeName "dosbox.exe"

; Path to the staged build directory containing dosbox.exe + Resources/
; Override via /DStagingDir="..."
#ifndef StagingDir
  #define StagingDir "..\..\augrudottir-dosbox-automation\dist\staging"
#endif

[Setup]
AppId={{6A2E8F3B-C4D1-4A7E-9B5F-1E3D7C8A2F4B}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppVerName={#MyAppName} {#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL=https://github.com/dosbox-automation/dosbox-automation/issues
AppUpdatesURL=https://github.com/dosbox-automation/dosbox-automation/releases

DefaultDirName={localappdata}\Programs\{#MyAppName}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
; Everything below resolves against the staging dir, so the script
; compiles unchanged both locally and on the builder VM.
LicenseFile={#StagingDir}\LICENSE
OutputDir={#StagingDir}\..
OutputBaseFilename=dosbox-automation-{#MyAppVersion}-windows-x64-setup
SetupIconFile={#StagingDir}\Resources\icons\windows\dosbox-automation.ico
UninstallDisplayIcon={app}\dosbox.exe

; Compression
Compression=lzma2/ultra64
SolidCompression=yes
LZMAUseSeparateProcess=yes

; Windows version requirements
MinVersion=10.0
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible

; User-level install (no admin required)
PrivilegesRequired=lowest

; Appearance
WizardStyle=modern
WizardSizePercent=100

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"
Name: "german"; MessagesFile: "compiler:Languages\German.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked

[Files]
; Main executable
Source: "{#StagingDir}\dosbox.exe"; DestDir: "{app}"; Flags: ignoreversion

; Resources tree (shaders, soundfonts, translations, drives, icons, webserver)
Source: "{#StagingDir}\Resources\*"; DestDir: "{app}\Resources"; Flags: ignoreversion recursesubdirs createallsubdirs

; Documentation
Source: "{#StagingDir}\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StagingDir}\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StagingDir}\THIRD_PARTY_LICENSES.txt"; DestDir: "{app}"; Flags: ignoreversion
Source: "{#StagingDir}\cheat-workbench.cmd"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
; Start Menu
Name: "{userprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Comment: "DOS emulator with HTTP REST API for automation"
Name: "{userprograms}\Start Cheat Workbench"; Filename: "{app}\cheat-workbench.cmd"; WorkingDir: "{app}"; IconFilename: "{app}\{#MyAppExeName}"; Comment: "Start dosbox-automation with the web API and open the Cheat Workbench"
Name: "{userprograms}\{cm:UninstallProgram,{#MyAppName}}"; Filename: "{uninstallexe}"

; Desktop icon (optional)
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; WorkingDir: "{app}"; Tasks: desktopicon

[Run]
; Option to launch after install
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent shellexec

[UninstallDelete]
; Config lives in %LOCALAPPDATA%\dosbox-automation (XDG convention on Windows)
; and is intentionally preserved on uninstall.

[Messages]
WelcomeLabel2=This will install [name/ver] on your computer.%n%ndosbox-automation is a DOSBox fork with an HTTP REST API for automated game installation, input recording and replay, and game launcher integration.%n%nThe application will be installed to your user folder and does not require administrator privileges.%n%nIt is recommended that you close all other applications before continuing.
FinishedLabelNoIcons=Setup has finished installing [name] on your computer.%n%nConfiguration files are stored in %%LOCALAPPDATA%%\dosbox-automation and will be preserved if you uninstall.
