# Compiles the WheelForge firmware for every board and lays the images out
# where the app looks for them.
#
#   .\build.ps1                 every target
#   .\build.ps1 -Target uno     just one
#   .\build.ps1 -List           show the targets and stop
#
# Needs arduino-cli:  winget install --id ArduinoSA.CLI -e
#
# The RP2040 core is not installed by default:
#   arduino-cli core install rp2040:rp2040 --additional-urls `
#     https://github.com/earlephilhower/arduino-pico/releases/download/global/package_rp2040_index.json

param(
    [string]$Target = "all",
    [switch]$Clean,
    [switch]$List
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
$imagesDir = Join-Path $root 'images'

# name, fqbn, output image, artefact extension, note
$targets = @(
    @{ n='leonardo'; fqbn='arduino:avr:leonardo';                    out='wheelforge-leonardo.hex'; ext='.ino.hex'; mode='HID' }
    @{ n='micro';    fqbn='arduino:avr:micro';                       out='wheelforge-micro.hex';    ext='.ino.hex'; mode='HID' }
    @{ n='pico';     fqbn='rp2040:rp2040:rpipico:usbstack=tinyusb';  out='wheelforge-pico.uf2';     ext='.ino.uf2'; mode='HID' }
    @{ n='esp32s3';  fqbn='esp32:esp32:esp32s3:USBMode=hwcdc';       out='wheelforge-esp32s3.bin';  ext='.ino.bin'; mode='HID' }
    @{ n='esp32s2';  fqbn='esp32:esp32:esp32s2';                     out='wheelforge-esp32s2.bin';  ext='.ino.bin'; mode='HID' }
    @{ n='uno';      fqbn='arduino:avr:uno';                         out='wheelforge-uno.hex';      ext='.ino.hex'; mode='bridge' }
    @{ n='mega2560'; fqbn='arduino:avr:mega';                        out='wheelforge-mega2560.hex'; ext='.ino.hex'; mode='bridge' }
    @{ n='nano';     fqbn='arduino:avr:nano';                        out='wheelforge-nano.hex';     ext='.ino.hex'; mode='bridge' }
    @{ n='esp32';    fqbn='esp32:esp32:esp32';                       out='wheelforge-esp32.bin';    ext='.ino.bin'; mode='bridge' }
    @{ n='esp8266';  fqbn='esp8266:esp8266:nodemcuv2';               out='wheelforge-esp8266.bin';  ext='.ino.bin'; mode='bridge' }
)

if ($List) {
    Write-Host "target      mode    fqbn" -ForegroundColor Cyan
    foreach ($t in $targets) { "{0,-11} {1,-7} {2}" -f $t.n, $t.mode, $t.fqbn }
    exit 0
}

$cli = Get-Command arduino-cli.exe -ErrorAction SilentlyContinue
if ($cli) {
    $arduinoCli = $cli.Source
} else {
    $guess = Join-Path $env:ProgramFiles 'Arduino CLI\arduino-cli.exe'
    if (Test-Path $guess) {
        $arduinoCli = $guess
    } else {
        Write-Host "arduino-cli not found. Install it with:" -ForegroundColor Red
        Write-Host "  winget install --id ArduinoSA.CLI -e" -ForegroundColor Yellow
        exit 1
    }
}

if ($Target -ne 'all') {
    $targets = @($targets | Where-Object { $_.n -eq $Target })
    if ($targets.Count -eq 0) {
        Write-Host "Unknown target '$Target'. Use -List to see them." -ForegroundColor Red
        exit 1
    }
}

# Cheap, and it catches the one class of bug that survives a clean compile: a
# report descriptor that disagrees with the struct actually being sent.
Write-Host "Checking HID descriptor..." -ForegroundColor Cyan
python (Join-Path $root 'check_descriptor.py')
if ($LASTEXITCODE -ne 0) {
    Write-Host "Descriptor check FAILED -- not building." -ForegroundColor Red
    exit 1
}

if ($Clean -and (Test-Path $imagesDir)) { Remove-Item $imagesDir -Recurse -Force }
if (-not (Test-Path $imagesDir)) { New-Item -ItemType Directory -Path $imagesDir | Out-Null }

$built = 0
$failed = @()

# Windows PowerShell wraps every stderr line from a native exe in an ErrorRecord
# and, under ErrorActionPreference 'Stop', throws on it even when the exe
# returned 0. The ESP8266 core prints its size report to stderr, so a perfectly
# good build was being reported as a failure. Exit codes are the truth here.
$ErrorActionPreference = 'Continue'

foreach ($t in $targets) {
    Write-Host ""
    Write-Host ("Building " + $t.n + "  [" + $t.mode + "]  " + $t.fqbn) -ForegroundColor Cyan

    $work = Join-Path $imagesDir ('_build_' + $t.n)
    $out = & $arduinoCli compile --fqbn $t.fqbn --output-dir $work (Join-Path $root 'wheelforge') 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host ($out | Select-Object -Last 8) -ForegroundColor Red
        $failed += $t.n
        if (Test-Path $work) { Remove-Item $work -Recurse -Force }
        continue
    }

    $out | Where-Object { $_ -match 'Sketch uses|Global variables' } |
        ForEach-Object { Write-Host ("  " + $_) -ForegroundColor DarkGray }

    # arduino-cli names artefacts after the sketch; the app wants them named
    # after the board. The bootloader-merged hex is not what we want.
    $art = Get-ChildItem $work -Filter ('*' + $t.ext) -ErrorAction SilentlyContinue |
           Where-Object { $_.Name -notlike '*with_bootloader*' } |
           Select-Object -First 1
    if (-not $art) {
        Write-Host ("  no " + $t.ext + " produced") -ForegroundColor Red
        $failed += $t.n
        Remove-Item $work -Recurse -Force
        continue
    }

    Copy-Item $art.FullName (Join-Path $imagesDir $t.out) -Force

    # ESP boards need the bootloader and partition table alongside the app image
    # to flash a blank chip, so keep them next to it.
    foreach ($extra in @('*.ino.bootloader.bin', '*.ino.partitions.bin')) {
        Get-ChildItem $work -Filter $extra -ErrorAction SilentlyContinue | ForEach-Object {
            $suffix = $_.Name -replace '^.*\.ino\.', ''
            Copy-Item $_.FullName (Join-Path $imagesDir ($t.n + '.' + $suffix)) -Force
        }
    }

    Remove-Item $work -Recurse -Force

    $kb = [math]::Round((Get-Item (Join-Path $imagesDir $t.out)).Length / 1KB, 1)
    Write-Host ("  -> " + $t.out + "  (" + $kb + " KB)") -ForegroundColor Green
    $built++
}

Write-Host ""
if ($failed.Count -gt 0) {
    Write-Host ("Built $built, FAILED: " + ($failed -join ', ')) -ForegroundColor Red
    exit 1
}
Write-Host ("Built $built image(s) into " + $imagesDir) -ForegroundColor Green
