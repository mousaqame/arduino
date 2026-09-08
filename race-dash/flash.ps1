# flash.ps1 - compile and upload a race-dash sketch.
#
# NodeMCU / ESP8266 (what you have). -Port is optional: with one board it is
# found automatically, and with several the boards are asked which sketch they
# are running so the right one is picked.
#   powershell -File flash.ps1 -Sketch racedash8266
#   powershell -File flash.ps1 -Sketch i2c_scan8266  -Port COM7
#
# ESP32, if you ever get one:
#   powershell -File flash.ps1 -Sketch racedash -Port COM7
#
# Compile without touching the board:
#   powershell -File flash.ps1 -Sketch racedash8266 -VerifyOnly
#
# The chip is chosen from the sketch name: anything ending in 8266 builds for
# NodeMCU, everything else for the ESP32. Override with -Fqbn if you need to.
#
# Kept deliberately ASCII-only: Windows PowerShell 5.1 reads .ps1 files as ANSI
# unless they carry a UTF-8 BOM, and a stray em-dash decodes into a smart quote
# that swallows the rest of the script.
#
# SAFETY: -Port is refused if it looks like an Arduino board. The steering
# wheel's Leonardo must never be written to - it runs EMC Lite, not a sketch,
# and an upload erases it.

param(
  [string]$Sketch     = "racedash8266",
  [string]$Port       = "",
  [string]$Fqbn       = "",
  [int]   $Baud       = 0,
  [switch]$VerifyOnly
)

$ErrorActionPreference = "Stop"

$Root      = Split-Path -Parent $MyInvocation.MyCommand.Path
$SketchDir = Join-Path $Root $Sketch
$SketchIno = Join-Path $SketchDir "$Sketch.ino"
$BuildDir  = Join-Path $Root ".build\$Sketch"

if (-not (Test-Path $SketchIno)) {
  $have = (Get-ChildItem $Root -Directory | Where-Object {
            Test-Path (Join-Path $_.FullName "$($_.Name).ino") } |
           Select-Object -ExpandProperty Name) -join ", "
  throw "No sketch at $SketchIno. Available: $have"
}

# --- pick the chip from the sketch name -----------------------------------
#   *_uno  -> Arduino Uno      *8266 -> NodeMCU      anything else -> ESP32
$IsUno     = $Sketch -match "_uno$"
$IsEsp8266 = $Sketch -match "8266$"
if (-not $Fqbn) {
  if     ($IsUno)     { $Fqbn = "arduino:avr:uno" }
  elseif ($IsEsp8266) { $Fqbn = "esp8266:esp8266:nodemcuv2" }
  else                { $Fqbn = "esp32:esp32:esp32" }
}
if ($Baud -eq 0) {
  if     ($IsUno)     { $Baud = 115200 }   # the Uno bootloader runs at this
  elseif ($IsEsp8266) { $Baud = 460800 }
  else                { $Baud = 921600 }
}

$ArduinoDir = "C:\Program Files (x86)\Arduino"
$Packages   = Join-Path $env:LOCALAPPDATA "Arduino15\packages"
$Builder    = Join-Path $ArduinoDir "arduino-builder.exe"

if (-not (Test-Path $Builder))  { throw "arduino-builder not found at $Builder" }
if (-not (Test-Path $Packages)) { throw "No Arduino15\packages - is the board core installed?" }

New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

Write-Host "Compiling $Sketch for $Fqbn ..." -ForegroundColor Cyan

& $Builder -compile -logger=human `
  -hardware (Join-Path $ArduinoDir "hardware") -hardware $Packages `
  -tools (Join-Path $ArduinoDir "tools-builder") `
  -tools (Join-Path $ArduinoDir "hardware\tools\avr") `
  -tools $Packages `
  -built-in-libraries (Join-Path $ArduinoDir "libraries") `
  -libraries (Join-Path $env:USERPROFILE "Documents\Arduino\libraries") `
  "-fqbn=$Fqbn" -build-path $BuildDir $SketchIno

if ($LASTEXITCODE -ne 0) { throw "Compile failed." }
Write-Host "Compiled." -ForegroundColor Green

if ($VerifyOnly) { return }

if (-not $Port) {
  # No port given: find it. With one board plugged in that is trivial; with
  # several, find_board.py asks each one what sketch it is running and picks
  # the one that matches, rather than guessing.
  Write-Host "No -Port given, looking for the board ..." -ForegroundColor Cyan
  $finder = Join-Path $Root "find_board.py"
  $Port = (& python $finder --sketch $Sketch | Select-Object -Last 1)
  if ($LASTEXITCODE -ne 0 -or -not $Port) {
    throw "Could not work out which port to use. Re-run with -Port COMx."
  }
  $Port = $Port.Trim()
  Write-Host "Using $Port" -ForegroundColor Green
}

# Refuse the steering wheel, and only the steering wheel. This used to reject
# every Arduino vendor ID, which was simple but also rejected the Uno once that
# became a supported dashboard board - so the block now names the Leonardo's
# own device IDs instead of its vendor. VID_0013 is EMC Lite itself.
$dev = Get-CimInstance Win32_PnPEntity |
  Where-Object { $_.Name -match "\($Port\)" } |
  Select-Object -First 1

$blockedBecause = $null
if ($dev) {
  $id = $dev.DeviceID
  if     ($id -match "VID_0013")                        { $blockedBecause = "EMC Dev - your steering wheel" }
  elseif ($id -match "VID_(2341|2A03)&PID_(0036|8036)") { $blockedBecause = "an Arduino Leonardo - your steering wheel" }
  elseif ($id -match "VID_1B4F&PID_(9203|9204)")        { $blockedBecause = "a SparkFun Pro Micro - Leonardo-class, appears as HID" }
}

if ($blockedBecause) {
  throw "$Port is $blockedBecause ($($dev.Name)). Flashing it would erase EMC Lite. Refusing."
}

Write-Host "Uploading to $Port at $Baud ..." -ForegroundColor Cyan

# esptool reports its failures on stderr, and those have to end up in $log for
# the diagnosis below to read them. Merging them in needs ErrorActionPreference
# relaxed first: under "Stop", PowerShell 5.1 turns a native command's stderr
# into a terminating NativeCommandError and the script dies before it can say
# anything useful.
$prevEAP = $ErrorActionPreference
$ErrorActionPreference = "Continue"

if ($IsUno) {
  # AVR uploads go through avrdude, which ships with the IDE rather than with
  # a downloaded core.
  $AvrBin  = Join-Path $ArduinoDir "hardware\tools\avr\bin\avrdude.exe"
  $AvrConf = Join-Path $ArduinoDir "hardware\tools\avr\etc\avrdude.conf"
  if (-not (Test-Path $AvrBin)) { throw "avrdude not found at $AvrBin" }

  $Hex = Join-Path $BuildDir "$Sketch.ino.hex"
  if (-not (Test-Path $Hex)) { throw "Missing $Hex - the compile produced no image." }

  $log = & $AvrBin -C $AvrConf -p atmega328p -c arduino -P $Port -b $Baud -D `
    -U "flash:w:${Hex}:i" 2>&1
} elseif ($IsEsp8266) {
  $Core = Get-ChildItem (Join-Path $Packages "esp8266\hardware\esp8266") -Directory |
    Sort-Object Name -Descending | Select-Object -First 1
  $Py = Get-ChildItem (Join-Path $Packages "esp8266\tools\python3") -Recurse -Filter "python3.exe" |
    Select-Object -First 1
  if (-not $Core) { throw "esp8266 core not found." }
  if (-not $Py)   { throw "bundled python3 not found under the esp8266 core." }

  $Upload = Join-Path $Core.FullName "tools\upload.py"
  $Bin    = Join-Path $BuildDir "$Sketch.ino.bin"
  if (-not (Test-Path $Bin)) { throw "Missing $Bin - the compile produced no image." }

  $log = & $Py.FullName $Upload --chip esp8266 --port $Port --baud $Baud `
    --before default_reset --after hard_reset write_flash 0x0 $Bin 2>&1
} else {
  $EspTool = Get-ChildItem (Join-Path $Packages "esp32\tools\esptool_py") -Recurse -Filter "esptool.exe" |
    Sort-Object FullName -Descending | Select-Object -First 1
  if (-not $EspTool) { throw "esptool.exe not found under the esp32 core." }

  # merged.bin already contains the bootloader, partition table and app at their
  # correct offsets, so one write at 0x0 replaces the usual four-file dance.
  $Merged = Join-Path $BuildDir "$Sketch.ino.merged.bin"
  if (-not (Test-Path $Merged)) { throw "Missing $Merged - the compile produced no merged image." }

  $log = & $EspTool.FullName --chip esp32 --port $Port --baud $Baud `
    write_flash --flash_mode dio --flash_freq 40m --flash_size detect 0x0 $Merged 2>&1
}

$uploadExit = $LASTEXITCODE
$ErrorActionPreference = $prevEAP

$log | ForEach-Object { Write-Host $_ }
$text = ($log | Out-String)

# The exit code alone is not trustworthy: the esp8266 core's upload.py wrapper
# can report success after esptool has already printed a fatal error. So the
# transcript has to agree as well - each tool prints its own marker only once
# the image is on the chip and has been read back.
# avrdude prints "avrdude done.  Thank you." on the way out even when it has
# just failed to open the port, so that line proves nothing. Only the readback
# line means the image is actually on the chip.
$successMark = if ($IsUno) { "bytes of flash verified" } else { "Hash of data verified" }

if ($uploadExit -ne 0 -or $text -notmatch $successMark) {
  if ($text -match "not in sync") {
    throw "Upload failed: the board on $Port did not respond as an Uno. Check it really is an Uno, and that nothing else is holding the port."
  }
  if ($text -match "can't open device") {
    throw "Upload failed: avrdude could not open $Port. Close the Arduino serial monitor and any running bridge, then try again."
  }
  # Order matters: a PermissionError message ALSO contains "could not open
  # port", so the busy case has to be tested before the missing case or every
  # in-use port gets misreported as an unplugged one.
  if ($text -match "Access is denied|PermissionError") {
    throw "Upload failed: $Port is open in another program. Close the Arduino serial monitor and stop any running forza_bridge.py, then try again."
  }
  if ($text -match "FileNotFoundError|could not open port") {
    throw "Upload failed: $Port is not there. Re-seat the USB cable, confirm the port with 'python forza_bridge.py --list-ports', and try again."
  }
  if ($text -match "Failed to connect") {
    throw "Upload failed: could not reach the board. Hold the FLASH button while it prints 'Connecting...', then release."
  }
  throw "Upload failed. See the esptool output above."
}

Write-Host "Done. $Sketch is running on $Port." -ForegroundColor Green
