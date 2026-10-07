; Inno Setup script for the Windows installer (VST3 + Standalone).
; Built by .github/workflows/release.yml:
;   iscc /DAppVersion=1.2.0 /DArtefacts=<repo>\build\PsyBox_artefacts\Release /O<out-dir> packaging\windows\PsyBox.iss

#ifndef AppVersion
  #define AppVersion "0.1.0"
#endif
#ifndef Artefacts
  #define Artefacts "..\..\build\PsyBox_artefacts\Release"
#endif

[Setup]
AppId={{C8C40419-B2C4-449C-884A-EF7218C1F1BD}
AppName=PsyBox
AppVersion={#AppVersion}
AppPublisher=Jonas Grunau
DefaultDirName={autopf}\PsyBox
DisableProgramGroupPage=yes
DisableDirPage=yes
OutputBaseFilename=PsyBox-{#AppVersion}-Windows-Setup
ArchitecturesAllowed=x64compatible
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
WizardStyle=modern

; The first type is the default: the standalone app starts unticked.
[Types]
Name: "plugin"; Description: "VST3 plug-in"
Name: "full"; Description: "VST3 plug-in and standalone app"
Name: "custom"; Description: "Custom"; Flags: iscustom

[Components]
Name: "vst3"; Description: "VST3 plug-in (Common Files\VST3)"; Types: plugin full
Name: "standalone"; Description: "Standalone app"; Types: full

[Files]
Source: "{#Artefacts}\VST3\PsyBox.vst3\*"; DestDir: "{commoncf64}\VST3\PsyBox.vst3"; Components: vst3; Flags: ignoreversion recursesubdirs createallsubdirs
Source: "{#Artefacts}\Standalone\PsyBox.exe"; DestDir: "{app}"; Components: standalone; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\PsyBox"; Filename: "{app}\PsyBox.exe"; Components: standalone
