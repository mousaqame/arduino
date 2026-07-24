<#
.SYNOPSIS
  Compile and upload an ESP8266 sketch headlessly using the Arduino IDE 1.8.x
  toolchain plus the installed esp8266 board core.

.EXAMPLE
  .\flash.ps1 robot -VerifyOnly     # compile only, don't touch the board
  .\flash.ps1 robot -Port COM5      # compile and upload
#>
param(
  [Parameter(Mandatory = $true)][string]$Sketch,
  [string]$Port = '',
  [string]$Fqbn = 'esp8266:esp8266:nodemcuv2',
  [int]   $Baud = 921600,
  [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'

$ide        = 'C:\Program Files (x86)\Arduino'
$pkgs       = Join-Path $env:LOCALAPPDATA 'Arduino15\packages'
$core       = Join-Path $pkgs 'esp8266\hardware\esp8266\3.1.2'
$py         = Join-Path $pkgs 'esp8266\tools\python3\3.7.2-post1\python3.exe'
$root       = $PSScriptRoot
$sketchDir  = Join-Path $root $Sketch
$sketchFile = Join-Path $sketchDir "$Sketch.ino"
$buildDir   = Join-Path $root ".build\$Sketch"
$sketchbook = Join-Path $env:USERPROFILE 'Documents\Arduino'

if (-not (Test-Path $sketchFile)) { throw "Sketch not found: $sketchFile" }
if (-not (Test-Path $core))       { throw "esp8266 core 3.1.2 not found at $core" }
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

Write-Host "==> Compiling $Sketch for $Fqbn" -ForegroundColor Cyan
& "$ide\arduino-builder.exe" `
  -compile `
  -hardware "$ide\hardware" `
  -hardware $pkgs `
  -tools "$ide\tools-builder" `
  -tools "$ide\hardware\tools\avr" `
  -tools $pkgs `
  -built-in-libraries "$ide\libraries" `
  -libraries "$sketchbook\libraries" `
  "-fqbn=$Fqbn" `
  -build-path $buildDir `
  -warnings=default `
  $sketchFile
if ($LASTEXITCODE -ne 0) { throw "Compile failed (exit $LASTEXITCODE)" }

$bin = Join-Path $buildDir "$Sketch.ino.bin"
if (-not (Test-Path $bin)) { throw "Expected binary not produced: $bin" }
"    binary: {0:N0} bytes" -f (Get-Item $bin).Length | Write-Host

if ($VerifyOnly) {
  Write-Host "==> Compile OK (verify-only, board untouched)" -ForegroundColor Green
  exit 0
}

# Find the board if no port was given. A NodeMCU shows up as CH340 or CP210x.
if (-not $Port) {
  $cand = Get-CimInstance Win32_PnPEntity |
          Where-Object { $_.Name -match 'COM\d+' -and $_.Name -match 'CH34|CP210|Silicon Labs|USB-SERIAL' } |
          Select-Object -First 1
  if (-not $cand) {
    throw "No NodeMCU-looking serial port found. Plug the board in, or pass -Port COMx."
  }
  if ($cand.Name -match '\((COM\d+)\)') { $Port = $Matches[1] }
  Write-Host "==> Found $($cand.Name)" -ForegroundColor Cyan
}

Write-Host "==> Uploading to $Port at $Baud" -ForegroundColor Cyan
& $py -I "$core\tools\upload.py" `
  --chip esp8266 --port $Port --baud $Baud `
  --before default_reset --after hard_reset `
  write_flash 0x0 $bin
if ($LASTEXITCODE -ne 0) { throw "Upload failed (exit $LASTEXITCODE)" }

Write-Host "==> Done: $Sketch is running on $Port" -ForegroundColor Green
Write-Host "    Open the serial monitor at 115200 to see its address." -ForegroundColor Green
