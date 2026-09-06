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

# Resolve VCPKG_ROOT before invoking CMake.
# Priority:
#   1. existing $env:VCPKG_ROOT
#   2. default candidates (C:\dev\vcpkg, $env:USERPROFILE\vcpkg)
# CMakeLists.txt reads $ENV{VCPKG_ROOT} to build the toolchain path.
if (-not $env:VCPKG_ROOT) {
    $candidates = @("C:\dev\vcpkg", "$env:USERPROFILE\vcpkg")
    foreach ($c in $candidates) {
        if (Test-Path "$c\scripts\buildsystems\vcpkg.cmake") {
            $env:VCPKG_ROOT = $c
            Write-Host "Using detected VCPKG_ROOT: $env:VCPKG_ROOT" -ForegroundColor Yellow
            break
        }
    }
}
if (-not $env:VCPKG_ROOT) {
    Write-Host "VCPKG_ROOT is not set and vcpkg was not found at default locations." -ForegroundColor Red
    Write-Host "Install vcpkg or set the VCPKG_ROOT environment variable." -ForegroundColor Red
    exit 1
}

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
