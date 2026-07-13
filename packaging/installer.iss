#define MyAppName "Chrome Bookmark Explorer"
#define MyAppVersion "0.2.2"
#define MyAppPublisher "hellomrli"
#define MyAppExeName "ChromeBookmarkExplorer.exe"

[Setup]
AppId={{A81B4B76-7972-4B1D-8F11-2CB9C1031E56}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\ChromeBookmarkExplorer
DefaultGroupName={#MyAppName}
OutputBaseFilename=ChromeBookmarkExplorer-Setup
Compression=lzma
SolidCompression=yes
WizardStyle=modern
ArchitecturesAllowed=x64
ArchitecturesInstallIn64BitMode=x64

[Files]
Source: "..\dist\ChromeBookmarkExplorer\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{commondesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "Create a desktop shortcut"; GroupDescription: "Additional icons:"

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "Launch {#MyAppName}"; Flags: nowait postinstall skipifsilent
