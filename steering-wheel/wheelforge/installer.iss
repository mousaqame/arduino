; WheelForge installer.
;
; Built by build.ps1 -Installer, which calls Inno Setup's ISCC.exe.
;
; Deliberately a per-user install: PrivilegesRequired=lowest means no UAC
; prompt, no admin account needed, and nothing written outside the user's own
; profile. A wheel configurator has no business asking for administrator.

#define AppName        "WheelForge"
#define AppVersion     "0.3.0"
#define AppPublisher   "Gravixar Workshop"
#define AppExeName     "WheelForge.exe"

[Setup]
AppId={{7D3A9C42-16BE-4F58-9A0D-2C7E5B1F84A3}
AppName={#AppName}
AppVersion={#AppVersion}
AppVerName={#AppName} {#AppVersion}
AppPublisher={#AppPublisher}
VersionInfoVersion={#AppVersion}

DefaultDirName={autopf}\{#AppName}
DefaultGroupName={#AppName}
DisableProgramGroupPage=yes
DisableDirPage=no
AllowNoIcons=yes

PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog

OutputDir=dist
OutputBaseFilename=WheelForge-Setup-{#AppVersion}
SetupIconFile=wheelforge.ico
UninstallDisplayIcon={app}\{#AppExeName}
UninstallDisplayName={#AppName} {#AppVersion}

; The app holds this mutex while it runs. Without it Setup happily deletes the
; install folder out from under a running copy, then cannot replace the locked
; executable -- which leaves a half-removed install and no way forward.
AppMutex=WheelForgeRunning
CloseApplications=yes
RestartApplications=no

Compression=lzma2/max
SolidCompression=yes
WizardStyle=modern
ArchitecturesInstallIn64BitMode=x64compatible

; Windows 7 SP1 is the floor, because that is where .NET Framework 4.x lands.
MinVersion=6.1sp1

LicenseFile=
InfoBeforeFile=

[Languages]
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "Create a &desktop shortcut"; GroupDescription: "Shortcuts:"

[Files]
Source: "build\{#AppExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "README.md";           DestDir: "{app}"; DestName: "README.txt"; Flags: ignoreversion isreadme

; Firmware images, so an installed copy can flash without the source tree.
; skipifsourcedoesntexist keeps the installer buildable before they are built.
Source: "firmware\images\*.hex"; DestDir: "{app}\firmware"; Flags: ignoreversion skipifsourcedoesntexist
Source: "firmware\images\*.bin"; DestDir: "{app}\firmware"; Flags: ignoreversion skipifsourcedoesntexist
Source: "firmware\images\*.uf2"; DestDir: "{app}\firmware"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#AppName}";              Filename: "{app}\{#AppExeName}"
Name: "{group}\Uninstall {#AppName}";    Filename: "{uninstallexe}"
Name: "{autodesktop}\{#AppName}";        Filename: "{app}\{#AppExeName}"; Tasks: desktopicon

[Run]
Filename: "{app}\{#AppExeName}"; Description: "Start {#AppName}"; Flags: nowait postinstall skipifsilent

[Code]

// Inno upgrades in place by default, which leaves any file an older version
// shipped and this one does not sitting in the install folder forever. Running
// the old uninstaller first means every install is a clean one.
// The GUID is spelled out rather than read back through SetupSetting('AppId'),
// which returns the literal setting text -- doubled leading brace and all --
// and so never matches the key Inno actually writes.
const
  PreviousAppKey =
    'Software\Microsoft\Windows\CurrentVersion\Uninstall\{7D3A9C42-16BE-4F58-9A0D-2C7E5B1F84A3}_is1';

function PreviousUninstaller(): String;
var
  value: String;
begin
  value := '';
  if not RegQueryStringValue(HKCU, PreviousAppKey, 'UninstallString', value) then
    if not RegQueryStringValue(HKLM, PreviousAppKey, 'UninstallString', value) then
      RegQueryStringValue(HKLM32, PreviousAppKey, 'UninstallString', value);
  Result := RemoveQuotes(value);
end;

procedure RemovePreviousVersion();
var
  uninstaller: String;
  folder: String;
  code: Integer;
begin
  uninstaller := PreviousUninstaller();
  if uninstaller = '' then Exit;
  if not FileExists(uninstaller) then Exit;

  folder := ExtractFileDir(uninstaller);

  Exec(uninstaller, '/VERYSILENT /SUPPRESSMSGBOXES /NORESTART',
       '', SW_HIDE, ewWaitUntilTerminated, code);

  // The uninstaller copies itself to a temp file and returns before that copy
  // has finished, so give it a moment before writing new files over the top.
  Sleep(1500);

  // Inno only removes what it logged, so anything an older build left behind --
  // a firmware image since renamed, say -- would survive. Clearing the folder
  // is what makes this a genuinely fresh install.
  //
  // Two guards, both learned the hard way:
  //
  //   The folder name must be ours, so someone who installed somewhere unusual
  //   does not have that directory deleted out from under them.
  //
  //   The executable must be GONE. If it still exists the uninstaller could not
  //   remove it, which means a copy is running and holding it open. Deleting
  //   the rest of the folder in that state destroys the install without being
  //   able to replace it -- exactly the failure this guard exists to prevent.
  if (folder = '') or not DirExists(folder) then Exit;
  if Lowercase(ExtractFileName(folder)) <> 'wheelforge' then Exit;
  if FileExists(AddBackslash(folder) + '{#AppExeName}') then Exit;

  DelTree(folder, True, True, True);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssInstall then RemovePreviousVersion();
end;

// WheelForge is a .NET Framework 4 app. Every Windows 10 and 11 install ships
// with 4.8, so this check only ever fires on an old or stripped down machine --
// but failing here with an explanation beats failing later with a silent
// "the application was unable to start correctly".
function IsDotNetFramework4Present(): Boolean;
var
  Release: Cardinal;
begin
  Result := RegQueryDWordValue(HKLM, 'SOFTWARE\Microsoft\NET Framework Setup\NDP\v4\Full',
                               'Release', Release) and (Release >= 378389);
end;

function InitializeSetup(): Boolean;
begin
  Result := True;
  if not IsDotNetFramework4Present() then
  begin
    if MsgBox('WheelForge needs the .NET Framework 4 runtime, which could not be found.'#13#10#13#10 +
              'It is part of Windows 10 and 11, so this is unusual. You can download it from'#13#10 +
              'microsoft.com/net/download/dotnet-framework'#13#10#13#10 +
              'Install anyway?', mbConfirmation, MB_YESNO) = IDNO then
      Result := False;
  end;
end;

