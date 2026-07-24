<#
.SYNOPSIS
  Compile and upload an Arduino sketch headlessly using the Arduino IDE 1.8.x toolchain.

.EXAMPLE
  .\flash.ps1 mysketch              # compile + upload to COM3
  .\flash.ps1 mysketch -VerifyOnly  # compile only, don't touch the board
#>
param(
  [Parameter(Mandatory = $true)][string]$Sketch,
  [string]$Port = 'COM8',
  [string]$Fqbn = 'arduino:avr:uno',
  [string]$Mcu  = 'atmega328p',
  [int]   $Baud = 115200,
  [switch]$VerifyOnly
)

$ErrorActionPreference = 'Stop'

$ide        = 'C:\Program Files (x86)\Arduino'
$root       = $PSScriptRoot
$sketchDir  = Join-Path $root $Sketch
$sketchFile = Join-Path $sketchDir "$Sketch.ino"
$buildDir   = Join-Path $root ".build\$Sketch"
$sketchbook = Join-Path $env:USERPROFILE 'Documents\Arduino'

if (-not (Test-Path $sketchFile)) { throw "Sketch not found: $sketchFile" }
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

Write-Host "==> Compiling $Sketch for $Fqbn" -ForegroundColor Cyan
& "$ide\arduino-builder.exe" `
  -compile `
  -hardware "$ide\hardware" `
  -hardware "$env:LOCALAPPDATA\Arduino15\packages" `
  -tools "$ide\tools-builder" `
  -tools "$ide\hardware\tools\avr" `
  -tools "$env:LOCALAPPDATA\Arduino15\packages" `
  -built-in-libraries "$ide\libraries" `
  -libraries "$sketchbook\libraries" `
  "-fqbn=$Fqbn" `
  -build-path $buildDir `
  -warnings=default `
  $sketchFile
if ($LASTEXITCODE -ne 0) { throw "Compile failed (exit $LASTEXITCODE)" }

$hex = Join-Path $buildDir "$Sketch.ino.hex"
if (-not (Test-Path $hex)) { throw "Expected hex not produced: $hex" }

& "$ide\hardware\tools\avr\bin\avr-size.exe" -C --mcu=$Mcu (Join-Path $buildDir "$Sketch.ino.elf") |
  Select-String 'Program:|Data:'

if ($VerifyOnly) {
  Write-Host "==> Compile OK (verify-only, board untouched)" -ForegroundColor Green
  exit 0
}

Write-Host "==> Uploading to $Port" -ForegroundColor Cyan
& "$ide\hardware\tools\avr\bin\avrdude.exe" `
  -C "$ide\hardware\tools\avr\etc\avrdude.conf" `
  -p $Mcu -c arduino -P $Port -b $Baud -D `
  -U "flash:w:${hex}:i"
if ($LASTEXITCODE -ne 0) { throw "Upload failed (exit $LASTEXITCODE)" }

Write-Host "==> Done: $Sketch is running on $Port" -ForegroundColor Green

