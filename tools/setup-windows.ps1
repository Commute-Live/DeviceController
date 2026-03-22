## LED Simulator — Windows Setup (PowerShell)
## Requires: MSYS2 (https://www.msys2.org)

$ErrorActionPreference = "Stop"

Write-Host "=== LED Simulator — Windows Setup ===" -ForegroundColor Cyan

# Check for MSYS2
$msys2 = "C:\msys64\usr\bin\bash.exe"
if (-Not (Test-Path $msys2)) {
    Write-Host "MSYS2 not found at C:\msys64." -ForegroundColor Red
    Write-Host "Download and install from: https://www.msys2.org" -ForegroundColor Yellow
    Write-Host "Then re-run this script." -ForegroundColor Yellow
    exit 1
}

Write-Host "MSYS2 found. Installing dependencies..."

# Install dependencies via MSYS2 pacman
& $msys2 -lc "pacman -S --noconfirm mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-libmosquitto mingw-w64-x86_64-pkg-config make"

# Create .env.simulator from example if it doesn't exist
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$envFile = Join-Path $scriptDir ".env.simulator"
$envExample = Join-Path $scriptDir ".env.simulator.example"
if (-Not (Test-Path $envFile)) {
    Copy-Item $envExample $envFile
    Write-Host "Created .env.simulator from example — edit it with your MQTT credentials."
} else {
    Write-Host ".env.simulator already exists, skipping."
}

# Build using MSYS2 MinGW shell
Write-Host "Building simulator..."
$toolsUnix = ($scriptDir -replace '\\', '/') -replace '^([A-Z]):', '/$1'
& "C:\msys64\mingw64.exe" bash -lc "cd '$toolsUnix' && make CXX=g++ clean all"

Write-Host ""
Write-Host "=== Setup complete ===" -ForegroundColor Green
Write-Host "Run the simulator:  .\tools\led_simulator.exe"
Write-Host "Keys: 1-5 = preset, R = cycle rows, S = cycle scenario, Q = quit"
