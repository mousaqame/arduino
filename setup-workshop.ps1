<#
.SYNOPSIS
  Put the whole Workshop on a PC in one step: Python, pyserial, the Arduino
  toolchain, the libraries, and this folder - then prove it works by compiling.

.DESCRIPTION
  Two roles, one file.

  On the machine that ALREADY WORKS (Mousa's), run:

      .\setup-workshop.ps1 -Pack -Out E:\

  That copies the Arduino toolchain, the libraries and this whole folder onto a
  USB stick, together with this script.

  On each NEW PC, plug the stick in and run (as Administrator):

      .\setup-workshop.ps1

  Everything else is automatic. Safe to run twice - each step checks before it
  acts, so a half-finished machine can just be run again.

.NOTES
  Why a USB stick and not a download: winget only carries Arduino IDE 2.x, which
  dropped `arduino-builder.exe`. Every flash.ps1 here is written against the
  1.8.x toolchain, so 2.x would install cleanly and then fail to compile. Copying
  the known-good 1.8.x folder sidesteps that, needs no venue wifi, and guarantees
  all five machines run the byte-identical toolchain.
#>

[CmdletBinding()]
param(
  # Build the USB stick instead of installing. Run this on the working machine.
  [switch]$Pack,

  # Where to write the stick (-Pack only).
  [string]$Out = '',

  # Where the Workshop folder lands on a target PC.
  [string]$Dest = 'C:\Workshop',

  # Skip the compile check at the end (it takes ~30s).
  [switch]$NoVerify
)

$ErrorActionPreference = 'Stop'
$here = $PSScriptRoot
$ideDir = 'C:\Program Files (x86)\Arduino'
$libDir = Join-Path $env:USERPROFILE 'Documents\Arduino\libraries'

function Say  ($m) { Write-Host "  $m" }
function Step ($m) { Write-Host "`n==> $m" -ForegroundColor Cyan }
function Ok   ($m) { Write-Host "  OK   $m" -ForegroundColor Green }
function Warn ($m) { Write-Host "  WARN $m" -ForegroundColor Yellow }
function Bad  ($m) { Write-Host "  FAIL $m" -ForegroundColor Red }

# --------------------------------------------------------------------------
# PACK - run on the machine that already works
# --------------------------------------------------------------------------

if ($Pack) {
  if (-not $Out) { throw "Say where to write it, e.g. -Out E:\" }
  if (-not (Test-Path $ideDir)) {
    throw "No Arduino toolchain at $ideDir - run -Pack on the machine that already compiles."
  }

  $stick = Join-Path $Out 'Workshop-Setup'
  New-Item -ItemType Directory -Force -Path $stick | Out-Null
  Step "Packing to $stick"

  Step 'Arduino toolchain'
  $ideZip = Join-Path $stick 'arduino-toolchain.zip'
  if (Test-Path $ideZip) { Remove-Item $ideZip -Force }
  Say 'compressing - this is the big one, give it a few minutes'
  Compress-Archive -Path "$ideDir\*" -DestinationPath $ideZip
  Ok ("arduino-toolchain.zip  {0:N0} MB" -f ((Get-Item $ideZip).Length / 1MB))

  Step 'Libraries'
  if (Test-Path $libDir) {
    $libZip = Join-Path $stick 'libraries.zip'
    if (Test-Path $libZip) { Remove-Item $libZip -Force }
    Compress-Archive -Path "$libDir\*" -DestinationPath $libZip
    Ok ("libraries.zip  {0:N0} MB" -f ((Get-Item $libZip).Length / 1MB))
  } else {
    Warn "No libraries at $libDir - target PCs will fail to compile the OLED sketches."
  }

  Step 'Workshop folder'
  $wsOut = Join-Path $stick 'Workshop'
  if (Test-Path $wsOut) { Remove-Item $wsOut -Recurse -Force }
  # .git and .build are rebuildable; leaving them out keeps the stick small.
  robocopy $here $wsOut /E /XD .git .build __pycache__ node_modules /NFL /NDL /NJH /NJS | Out-Null
  Ok "Workshop folder copied"

  Copy-Item $PSCommandPath (Join-Path $stick 'setup-workshop.ps1') -Force
  Ok 'setup-workshop.ps1 copied'

  Write-Host "`nDone. On each new PC: open PowerShell as Administrator in" -ForegroundColor Green
  Write-Host "$stick and run  .\setup-workshop.ps1" -ForegroundColor Green
  exit 0
}

# --------------------------------------------------------------------------
# INSTALL - run on each new PC
# --------------------------------------------------------------------------

Write-Host "`nWorkshop setup" -ForegroundColor Cyan
Write-Host "This installs Python, the Arduino toolchain and the Workshop itself."

$admin = ([Security.Principal.WindowsPrincipal] `
          [Security.Principal.WindowsIdentity]::GetCurrent()
         ).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
if (-not $admin) {
  Bad 'Not running as Administrator.'
  Say 'The toolchain goes into Program Files, which needs it.'
  Say 'Right-click PowerShell -> Run as Administrator, then run this again.'
  exit 1
}

$problems = @()

# ---- 1. Python -----------------------------------------------------------

Step '1/6  Python'
function Test-Python ($exe) {
  # Ask it. Inspecting the file lies both ways: the WindowsApps python.exe is a
  # 0-byte execution alias that works fine, while plenty of real python.exe files
  # belong to something else entirely (Blender ships one).
  if (-not $exe) { return $false }
  $v = & $exe --version 2>$null
  return ($LASTEXITCODE -eq 0 -and $v -match 'Python 3\.\d+')
}

function Find-Python {
  foreach ($name in 'python', 'python3') {
    $c = Get-Command $name -ErrorAction SilentlyContinue
    if ($c -and (Test-Python $c.Source)) { return $c.Source }
  }
  # Only right after a winget install, when PATH hasn't caught up in this shell.
  # Deliberately narrow: a wide recursive search finds other apps' bundled Pythons.
  foreach ($p in @(
      "$env:LOCALAPPDATA\Programs\Python\Python3*\python.exe",
      "$env:LOCALAPPDATA\Python\pythoncore-3*\python.exe",
      'C:\Program Files\Python3*\python.exe')) {
    $hit = Get-Item $p -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($hit -and (Test-Python $hit.FullName)) { return $hit.FullName }
  }
  return $null
}

$py = Find-Python
if ($py) {
  Ok "$py  ($(& $py --version 2>&1))"
} else {
  Say 'not found - installing'
  winget install --id Python.Python.3.13 --silent `
    --accept-package-agreements --accept-source-agreements | Out-Null
  # winget updates PATH for NEW shells, not this one, so look on disk.
  $py = Find-Python
  if ($py) { Ok "installed: $py" }
  else { Bad 'Python install failed'; $problems += 'python' }
}

# ---- 2. pyserial ---------------------------------------------------------

Step '2/6  pyserial'
if ($py) {
  $has = & $py -c "import serial; print(serial.__version__)" 2>$null
  if ($LASTEXITCODE -eq 0) {
    Ok "pyserial $has"
  } else {
    & $py -m pip install --quiet pyserial
    $has = & $py -c "import serial; print(serial.__version__)" 2>$null
    if ($LASTEXITCODE -eq 0) { Ok "pyserial $has installed" }
    else { Bad 'pyserial install failed'; $problems += 'pyserial' }
  }
} else {
  Bad 'skipped - no Python'
}

# ---- 3. Arduino toolchain ------------------------------------------------

Step '3/6  Arduino toolchain (1.8.x)'
$builder = Join-Path $ideDir 'arduino-builder.exe'
if (Test-Path $builder) {
  Ok "already at $ideDir"
} else {
  $zip = Join-Path $here 'arduino-toolchain.zip'
  if (Test-Path $zip) {
    Say 'extracting - a few minutes'
    New-Item -ItemType Directory -Force -Path $ideDir | Out-Null
    Expand-Archive -Path $zip -DestinationPath $ideDir -Force
    if (Test-Path $builder) { Ok "installed to $ideDir" }
    else { Bad 'extracted but arduino-builder.exe is missing'; $problems += 'toolchain' }
  } else {
    Bad "arduino-toolchain.zip not found next to this script"
    Say 'Run  .\setup-workshop.ps1 -Pack -Out E:\  on the working machine first.'
    Say 'Do NOT install Arduino IDE 2.x instead - it has no arduino-builder.exe'
    Say 'and every flash.ps1 here needs it.'
    $problems += 'toolchain'
  }
}

# ---- 4. Libraries --------------------------------------------------------

Step '4/6  Libraries'
$needed = @('Adafruit_GFX_Library', 'Adafruit_SSD1306', 'DHT_sensor_library')
$zip = Join-Path $here 'libraries.zip'
if (Test-Path $zip) {
  New-Item -ItemType Directory -Force -Path $libDir | Out-Null
  Expand-Archive -Path $zip -DestinationPath $libDir -Force
  Ok "extracted to $libDir"
} elseif (Test-Path $libDir) {
  Say 'no libraries.zip - checking what is already here'
} else {
  Bad 'no libraries.zip and no library folder'
  $problems += 'libraries'
}

if (Test-Path $libDir) {
  $have = (Get-ChildItem $libDir -Directory -ErrorAction SilentlyContinue).Name
  foreach ($n in $needed) {
    # Library Manager and hand-copies disagree about spaces vs underscores.
    $key = $n -replace '[_ ]', ''
    if ($have | Where-Object { ($_ -replace '[_ ]', '') -like "*$key*" }) { Ok $n }
    else { Warn "$n missing - sketches using it will not compile"; $problems += "lib:$n" }
  }
}

# ---- 5. The Workshop folder ---------------------------------------------

Step "5/6  Workshop folder -> $Dest"
$src = if (Test-Path (Join-Path $here 'Workshop')) { Join-Path $here 'Workshop' } else { $here }
if ((Resolve-Path $src).Path -eq (Resolve-Path $Dest -ErrorAction SilentlyContinue).Path) {
  Ok 'already in place'
} else {
  New-Item -ItemType Directory -Force -Path $Dest | Out-Null
  robocopy $src $Dest /E /XD .git .build __pycache__ /NFL /NDL /NJH /NJS | Out-Null
  if (Test-Path (Join-Path $Dest 'hub\hub.py')) { Ok "copied to $Dest" }
  else { Bad "copy failed - no hub\hub.py at $Dest"; $problems += 'workshop' }
}

# ---- 6. Prove it ---------------------------------------------------------

Step '6/6  Compile check'
$flash = Join-Path $Dest 'arduino\flash.ps1'
if ($NoVerify) {
  Say 'skipped (-NoVerify)'
} elseif ((Test-Path $flash) -and (Test-Path $builder)) {
  Say 'compiling parking_serial - this is the real test of every step above'
  Push-Location (Split-Path $flash)
  try {
    & powershell -NoProfile -ExecutionPolicy Bypass -File $flash `
        -Sketch parking_serial -VerifyOnly *>&1 | Select-Object -Last 4
    if ($LASTEXITCODE -eq 0) { Ok 'compiles - this PC is ready' }
    else { Bad "compile failed (exit $LASTEXITCODE)"; $problems += 'compile' }
  } finally { Pop-Location }
} else {
  Warn 'skipped - flash.ps1 or the toolchain is missing'
}

# ---- which board is on which port ---------------------------------------

if ($py) {
  Step 'Boards plugged in right now'
  & $py -c @"
try:
    from serial.tools import list_ports
    found = [p for p in list_ports.comports()]
    if not found:
        print('  none - plug an Arduino in, then re-run this bit')
    for p in found:
        print(f'  {p.device}  {p.description}')
except Exception as e:
    print('  could not check:', e)
"@
  Say 'If a port here is not the default in that project flash.ps1, pass -Port COMx.'
}

# ---- summary -------------------------------------------------------------

Write-Host ''
if ($problems.Count -eq 0) {
  Write-Host 'READY.' -ForegroundColor Green
  Write-Host "Double-click  $Dest\Start Workshop.bat  to open the hub." -ForegroundColor Green
} else {
  Write-Host ("NOT READY - " + ($problems -join ', ')) -ForegroundColor Red
  Write-Host 'Fix the FAIL lines above and run this again; it will skip what already worked.'
}
