<#
.SYNOPSIS
  Create a Desktop shortcut for the dashboard, and optionally start it at login.

.EXAMPLE
  .\install-shortcuts.ps1              # Desktop shortcut only
  .\install-shortcuts.ps1 -AtLogin     # also start automatically at login
  .\install-shortcuts.ps1 -Remove      # remove both shortcuts
#>
param(
  [switch]$AtLogin,
  [switch]$Remove
)

$ErrorActionPreference = 'Stop'

$target  = Join-Path $PSScriptRoot 'start-dashboard.bat'
$name    = 'Parking Sensor Dashboard.lnk'
$desktop = Join-Path ([Environment]::GetFolderPath('Desktop')) $name
$startup = Join-Path ([Environment]::GetFolderPath('Startup')) $name

if ($Remove) {
  foreach ($p in @($desktop, $startup)) {
    if (Test-Path $p) { Remove-Item $p -Force; Write-Host "removed $p" }
    else { Write-Host "not present $p" }
  }
  exit 0
}

if (-not (Test-Path $target)) { throw "Missing $target" }

function New-Shortcut([string]$path) {
  $shell = New-Object -ComObject WScript.Shell
  $sc = $shell.CreateShortcut($path)
  $sc.TargetPath       = $target
  $sc.WorkingDirectory = $PSScriptRoot
  $sc.Description      = 'Live dashboard for the Arduino parking sensor'
  $sc.Save()
  Write-Host "created $path"
}

New-Shortcut $desktop
if ($AtLogin) {
  New-Shortcut $startup
  Write-Host "Dashboard will start at login. Undo with: .\install-shortcuts.ps1 -Remove"
}
