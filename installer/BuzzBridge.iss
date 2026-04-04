; BuzzBridge VST3 - Inno Setup Installer Script
; Installs the BuzzBridge.vst3 bundle to the system VST3 directory
; and extracts the machine database to %USERPROFILE%\Buzz\Gear.
;
; Build with: iscc /DMyAppVersion=v1.0.0 BuzzBridge.iss
; Expects the assembled bundle in ../dist/BuzzBridge.vst3/
; and the machine database zip in ../data/mdb_machines.zip

#ifndef MyAppVersion
  #define MyAppVersion "v0.0.0-dev"
#endif

#define MyAppName "BuzzBridge VST3"
#define MyAppPublisher "BuzzBridge Contributors"
#define MyAppURL "https://github.com/nstarke/JeskolaBuzzVST"

[Setup]
AppId={{7A4E534B-4F4C-4142-555A-5A47454E3031}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}/issues
DefaultDirName={commoncf}\VST3\BuzzBridge.vst3
DirExistsWarning=no
DisableProgramGroupPage=yes
LicenseFile=LICENSE.txt
OutputDir=..\dist
OutputBaseFilename=BuzzBridge-Setup-{#MyAppVersion}
Compression=lzma2
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
UninstallDisplayName={#MyAppName}
WizardStyle=modern

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Types]
Name: "full"; Description: "Full installation (plugin + machine database)"
Name: "compact"; Description: "Plugin only"
Name: "custom"; Description: "Custom installation"; Flags: iscustom

[Components]
Name: "plugin"; Description: "BuzzBridge VST3 Plugin"; Types: full compact custom; Flags: fixed
Name: "machines"; Description: "Buzz Machine Database (726 machines, extracts to %USERPROFILE%\Buzz\Gear)"; Types: full

[Files]
; 64-bit plugin and bridge host
Source: "..\dist\BuzzBridge.vst3\Contents\x86_64-win\BuzzBridge.vst3"; DestDir: "{app}\Contents\x86_64-win"; Components: plugin; Flags: ignoreversion
Source: "..\dist\BuzzBridge.vst3\Contents\x86_64-win\BuzzBridgeHost32.exe"; DestDir: "{app}\Contents\x86_64-win"; Components: plugin; Flags: ignoreversion

; 32-bit plugin
Source: "..\dist\BuzzBridge.vst3\Contents\x86-win\BuzzBridge.vst3"; DestDir: "{app}\Contents\x86-win"; Components: plugin; Flags: ignoreversion

; Resources
Source: "..\dist\BuzzBridge.vst3\Contents\Resources\moduleinfo.json"; DestDir: "{app}\Contents\Resources"; Components: plugin; Flags: ignoreversion

; Bundle metadata (optional files)
Source: "..\dist\BuzzBridge.vst3\desktop.ini"; DestDir: "{app}"; Components: plugin; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\BuzzBridge.vst3\PlugIn.ico"; DestDir: "{app}"; Components: plugin; Flags: ignoreversion skipifsourcedoesntexist

; Machine database zip — extracted in [Code] section
Source: "..\data\mdb_machines.zip"; DestDir: "{tmp}"; Components: machines; Flags: deleteafterinstall skipifsourcedoesntexist

[Icons]
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{app}"

[Code]
procedure ExtractMachineDatabase();
var
  ZipPath: String;
  DestDir: String;
  Shell: Variant;
  ZipFile: Variant;
  DestFolder: Variant;
begin
  ZipPath := ExpandConstant('{tmp}\mdb_machines.zip');
  DestDir := ExpandConstant('{%USERPROFILE}\Buzz\Gear');

  if not FileExists(ZipPath) then
    Exit;

  { Create destination directory }
  if not DirExists(DestDir) then
    ForceDirectories(DestDir);

  { Use Windows Shell to extract the zip }
  try
    Shell := CreateOleObject('Shell.Application');
    ZipFile := Shell.NameSpace(ZipPath);
    DestFolder := Shell.NameSpace(DestDir);

    { Extract all items (4+16 = no progress dialog + yes to all) }
    DestFolder.CopyHere(ZipFile.Items, 4 + 16);
  except
    Log('Failed to extract machine database: ' + GetExceptionMessage);
  end;
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
  begin
    if WizardIsComponentSelected('machines') then
    begin
      ExtractMachineDatabase();
    end;
  end;
end;
