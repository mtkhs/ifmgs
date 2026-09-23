# Simple ifmgs/axmgs Build Script

param(
    [Parameter(Mandatory=$false)]
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",

    [Parameter(Mandatory=$false)]
    [switch]$Rebuild
)

Write-Host "Building ifmgs/axmgs - Configuration: $Configuration" -ForegroundColor Cyan

# Get script directory
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Definition
Set-Location $ScriptDir

# Configure if needed
if (-not (Test-Path "build\CMakeCache.txt")) {
    Write-Host "Configuring CMake..." -ForegroundColor Yellow
    cmake -B build -G "Visual Studio 17 2022" -A x64
}

# Build
Write-Host "Building..." -ForegroundColor Yellow
if ($Rebuild) {
    cmake --build build --config $Configuration --clean-first
} else {
    cmake --build build --config $Configuration
}

# Check result
if ($LASTEXITCODE -eq 0) {
    Write-Host "Build successful!" -ForegroundColor Green
    $OutputDir = "build\$Configuration"
    if (Test-Path "$OutputDir\ifmgs.sph") { Write-Host "  - ifmgs.sph" -ForegroundColor Green }
    if (Test-Path "$OutputDir\axmgs.sph") { Write-Host "  - axmgs.sph" -ForegroundColor Green }
} else {
    Write-Host "Build failed!" -ForegroundColor Red
    exit 1
}
