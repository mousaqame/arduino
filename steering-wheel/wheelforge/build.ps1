# Builds WheelForge.exe with the C# compiler that ships inside Windows.
#
# There is deliberately no SDK, no NuGet and no project file. csc.exe lives in
# the .NET Framework folder on every Windows install, and the executable it
# produces runs anywhere .NET Framework 4.x is present -- which is every Windows
# 10 and 11 machine, out of the box. That is what makes the result a single file
# you can copy to another PC and just run.
#
#   .\build.ps1                 compile
#   .\build.ps1 -Run            compile and launch
#   .\build.ps1 -Installer      compile and build dist\WheelForge-Setup-x.y.z.exe
#   .\build.ps1 -Debug          compile unoptimised, with symbols

param(
    [switch]$Run,
    [switch]$Installer,
    [switch]$Debug
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path

$csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework64\v4.0.30319\csc.exe'
if (-not (Test-Path $csc)) {
    $csc = Join-Path $env:WINDIR 'Microsoft.NET\Framework\v4.0.30319\csc.exe'
}
if (-not (Test-Path $csc)) {
    Write-Host "Could not find csc.exe. This needs .NET Framework 4.x, which is" -ForegroundColor Red
    Write-Host "normally part of Windows." -ForegroundColor Red
    exit 1
}

$outDir = Join-Path $root 'build'
if (-not (Test-Path $outDir)) { New-Item -ItemType Directory -Path $outDir | Out-Null }
$exe = Join-Path $outDir 'WheelForge.exe'

# --- icon -------------------------------------------------------------------
# Generated rather than committed as a binary, so it stays editable alongside
# everything else. See tools\IconGen.cs.

$icon = Join-Path $root 'wheelforge.ico'
if (-not (Test-Path $icon)) {
    Write-Host "Generating icon..." -ForegroundColor DarkGray
    $iconGen = Join-Path $root 'tools\IconGen.exe'
    if (-not (Test-Path $iconGen)) {
        & $csc /nologo /target:exe ('/out:' + $iconGen) /reference:System.dll `
               /reference:System.Drawing.dll (Join-Path $root 'tools\IconGen.cs')
        if ($LASTEXITCODE -ne 0) { Write-Host "Icon compiler failed." -ForegroundColor Red; exit 1 }
    }
    & $iconGen $icon | Out-Null
}

# --- compile ----------------------------------------------------------------

$sources = Get-ChildItem -Path (Join-Path $root 'src') -Filter *.cs -Recurse |
           ForEach-Object { $_.FullName }

if ($sources.Count -eq 0) {
    Write-Host "No source files found under src\." -ForegroundColor Red
    exit 1
}

Write-Host "WheelForge build" -ForegroundColor Cyan
Write-Host ("  compiler : " + $csc) -ForegroundColor DarkGray
Write-Host ("  sources  : " + $sources.Count + " files") -ForegroundColor DarkGray
Write-Host ("  output   : " + $exe) -ForegroundColor DarkGray
Write-Host ""

$refs = @(
    'System.dll',
    'System.Core.dll',
    'System.Drawing.dll',
    'System.Windows.Forms.dll'
)
# System.IO.Ports.SerialPort -- used for the 1200 baud bootloader touch -- lives
# in System.dll on .NET Framework. The standalone System.IO.Ports assembly is a
# .NET Core thing and referencing it here fails.

$cscArgs = @(
    '/nologo',
    '/target:winexe',
    ('/out:' + $exe),
    ('/win32manifest:' + (Join-Path $root 'app.manifest')),
    ('/win32icon:' + $icon),
    '/platform:anycpu',
    '/langversion:5',
    '/warnaserror-'
)

if ($Debug) {
    $cscArgs += '/debug+'
    $cscArgs += '/define:DEBUG'
} else {
    $cscArgs += '/optimize+'
}

foreach ($r in $refs) { $cscArgs += ('/reference:' + $r) }
$cscArgs += $sources

& $csc $cscArgs
$code = $LASTEXITCODE

Write-Host ""
if ($code -ne 0) {
    Write-Host "Build FAILED (exit $code)" -ForegroundColor Red
    exit $code
}

$size = [math]::Round((Get-Item $exe).Length / 1KB, 1)
Write-Host ("Build OK  ->  " + $exe + "  (" + $size + " KB)") -ForegroundColor Green

# --- installer --------------------------------------------------------------

if ($Installer) {
    # winget installs Inno Setup per-user by default, which puts it under
    # LOCALAPPDATA rather than Program Files.
    $iscc = $null
    $candidates = @(
        "$env:LOCALAPPDATA\Programs\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 6\ISCC.exe",
        "$env:ProgramFiles\Inno Setup 6\ISCC.exe",
        "${env:ProgramFiles(x86)}\Inno Setup 5\ISCC.exe"
    )
    foreach ($c in $candidates) { if (Test-Path $c) { $iscc = $c; break } }

    if (-not $iscc) {
        $onPath = Get-Command ISCC.exe -ErrorAction SilentlyContinue
        if ($onPath) { $iscc = $onPath.Source }
    }

    if (-not $iscc) {
        Write-Host ""
        Write-Host "Inno Setup not found. Install it with:" -ForegroundColor Red
        Write-Host "  winget install --id JRSoftware.InnoSetup -e" -ForegroundColor Yellow
        exit 1
    }

    Write-Host ""
    Write-Host "Building installer..." -ForegroundColor Cyan
    & $iscc /Q (Join-Path $root 'installer.iss')
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Installer FAILED (exit $LASTEXITCODE)" -ForegroundColor Red
        exit $LASTEXITCODE
    }

    $setup = Get-ChildItem (Join-Path $root 'dist') -Filter 'WheelForge-Setup-*.exe' |
             Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $setupKb = [math]::Round($setup.Length / 1KB, 1)
    Write-Host ("Installer OK  ->  " + $setup.FullName + "  (" + $setupKb + " KB)") -ForegroundColor Green
}

if ($Run) {
    Write-Host "Launching..." -ForegroundColor DarkGray
    Start-Process $exe
}


