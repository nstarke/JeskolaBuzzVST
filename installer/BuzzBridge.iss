; BuzzBridge VST3 - Inno Setup Installer Script
; Installs two separate bundles:
;   - 64-bit bundle → Program Files\Common Files\VST3\BuzzBridge.vst3
;   - 32-bit bundle → Program Files (x86)\Common Files\VST3\BuzzBridge.vst3
; and extracts the machine database to %USERPROFILE%\Buzz\Gear.
;
; Build with: iscc /DMyAppVersion=v1.0.0 BuzzBridge.iss
; Expects the assembled bundle in ../dist/BuzzBridge.vst3/
; and the machine database zip in ../data/mdb_machines.zip
;
; NOTE on bundle layout:
;   The Steinberg spec allows a single VST3 bundle to contain multiple
;   architecture subfolders (x86-win + x86_64-win). Most hosts (Reaper,
;   Ableton, Cubase) handle this correctly — they load only the subfolder
;   matching their own wordsize. But JUCE-based hosts (including
;   BespokeSynth) `recursiveFileSearch` the bundle, find BOTH inner
;   BuzzBridge.vst3 files, alphabetically try x86-win first, silently
;   fail on the wrong-arch LoadLibrary, and then crash when the failed
;   DLLHandle returns a null factory. To work around the JUCE bug, we
;   install the two architectures to SEPARATE Common Files locations so
;   a 64-bit host only ever sees the 64-bit bundle and vice versa.

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
; {commoncf64} = C:\Program Files\Common Files on 64-bit Windows.
; Individual bundle files go under BOTH {commoncf64}\VST3\BuzzBridge.vst3
; and {commoncf32}\VST3\BuzzBridge.vst3 — see [Files].
DefaultDirName={commoncf64}\VST3\BuzzBridge.vst3
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
; -----------------------------------------------------------------------
; 64-bit bundle: {commoncf64}\VST3\BuzzBridge.vst3
;   Contains ONLY the x86_64-win subfolder. Resources and moduleinfo go
;   here too. 32-bit hosts should NOT look at this bundle; if they do
;   (some do) they'll find no matching arch and skip it.
; -----------------------------------------------------------------------
Source: "..\dist\BuzzBridge.vst3\Contents\x86_64-win\BuzzBridge.vst3"; DestDir: "{commoncf64}\VST3\BuzzBridge.vst3\Contents\x86_64-win"; Components: plugin; Flags: ignoreversion
Source: "..\dist\BuzzBridge.vst3\Contents\x86_64-win\BuzzBridgeHost32.exe"; DestDir: "{commoncf64}\VST3\BuzzBridge.vst3\Contents\x86_64-win"; Components: plugin; Flags: ignoreversion
Source: "..\dist\BuzzBridge.vst3\Contents\Resources\moduleinfo.json"; DestDir: "{commoncf64}\VST3\BuzzBridge.vst3\Contents\Resources"; Components: plugin; Flags: ignoreversion
Source: "..\dist\BuzzBridge.vst3\desktop.ini"; DestDir: "{commoncf64}\VST3\BuzzBridge.vst3"; Components: plugin; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\BuzzBridge.vst3\PlugIn.ico"; DestDir: "{commoncf64}\VST3\BuzzBridge.vst3"; Components: plugin; Flags: ignoreversion skipifsourcedoesntexist

; -----------------------------------------------------------------------
; 32-bit bundle: {commoncf32}\VST3\BuzzBridge.vst3
;   Contains ONLY the x86-win subfolder. BuzzBridgeHost32.exe is also
;   copied here so the 32-bit plugin can find it when loaded directly
;   (32-bit plugin loads 32-bit Buzz machines in-process and technically
;   doesn't need the bridge host, but having it here keeps the layout
;   uniform).
; -----------------------------------------------------------------------
Source: "..\dist\BuzzBridge.vst3\Contents\x86-win\BuzzBridge.vst3"; DestDir: "{commoncf32}\VST3\BuzzBridge.vst3\Contents\x86-win"; Components: plugin; Flags: ignoreversion
Source: "..\dist\BuzzBridge.vst3\Contents\Resources\moduleinfo.json"; DestDir: "{commoncf32}\VST3\BuzzBridge.vst3\Contents\Resources"; Components: plugin; Flags: ignoreversion
Source: "..\dist\BuzzBridge.vst3\desktop.ini"; DestDir: "{commoncf32}\VST3\BuzzBridge.vst3"; Components: plugin; Flags: ignoreversion skipifsourcedoesntexist
Source: "..\dist\BuzzBridge.vst3\PlugIn.ico"; DestDir: "{commoncf32}\VST3\BuzzBridge.vst3"; Components: plugin; Flags: ignoreversion skipifsourcedoesntexist

; Machine database zip — extracted in [Code] section
Source: "..\data\mdb_machines.zip"; DestDir: "{tmp}"; Components: machines; Flags: deleteafterinstall skipifsourcedoesntexist

[Icons]
Name: "{group}\Uninstall {#MyAppName}"; Filename: "{uninstallexe}"

[UninstallDelete]
Type: filesandordirs; Name: "{commoncf64}\VST3\BuzzBridge.vst3"
Type: filesandordirs; Name: "{commoncf32}\VST3\BuzzBridge.vst3"
Type: filesandordirs; Name: "{app}"

[InstallDelete]
; Remove any legacy dual-arch install at {commoncf64}\VST3\BuzzBridge.vst3
; from before we split the bundles. The old layout had x86-win inside the
; 64-bit bundle which triggers a JUCE scanner crash in BespokeSynth.
Type: filesandordirs; Name: "{commoncf64}\VST3\BuzzBridge.vst3\Contents\x86-win"
Type: filesandordirs; Name: "{commoncf32}\VST3\BuzzBridge.vst3\Contents\x86_64-win"

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
