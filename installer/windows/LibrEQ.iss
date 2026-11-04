; Inno Setup script. Build with: iscc /DAppVersion=x.y.z /DSourceDir=path LibrEQ.iss
[Setup]
AppId={{7E2C4B1A-6D3F-4E5A-9C21-4C1B0E8A7F10}
AppName=LibrEQ
AppVersion={#AppVersion}
AppPublisher=LibrEQ Community
DefaultDirName={commoncf64}\VST3
DisableDirPage=yes
DisableProgramGroupPage=yes
DirExistsWarning=no
PrivilegesRequired=admin
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
OutputBaseFilename=LibrEQ-{#AppVersion}-Windows-x64-Setup
OutputDir=.
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
UninstallDisplayName=LibrEQ

[Files]
Source: "{#SourceDir}\LibrEQ.vst3\*"; DestDir: "{commoncf64}\VST3\LibrEQ.vst3"; Flags: recursesubdirs createallsubdirs ignoreversion

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\LibrEQ.vst3"
