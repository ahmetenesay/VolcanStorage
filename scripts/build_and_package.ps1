<#
.SYNOPSIS
    VolcanStorage Master Build & Packaging Orchestrator (Windows & WSL Linux)
.DESCRIPTION
    Automates CMake configuration, compilation, staging, and Zstandard-22 packaging
    for all 6 target distributions:
      - Windows-x64
      - Windows-arm64
      - Linux-x64
      - Linux-arm64
      - macOS-x64
      - macOS-arm64
#>

[CmdletBinding()]
param(
    [switch]$SkipWindows,
    [switch]$SkipLinux,
    [switch]$SkipPackaging
)

$ErrorActionPreference = "Stop"
$RepoRoot = (Get-Item -Path $PSScriptRoot).Parent.FullName
Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "       VolcanStorage Automated Multiplatform Build System        " -ForegroundColor Cyan
Write-Host "=================================================================" -ForegroundColor Cyan
Write-Host "Repository Root: $RepoRoot" -ForegroundColor Gray

# -----------------------------------------------------------------------------
# 1. Detect CMake and Visual Studio on Windows Host
# -----------------------------------------------------------------------------
$CMakeExe = "cmake.exe"
if (-not (Get-Command $CMakeExe -ErrorAction SilentlyContinue)) {
    $VsCMake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
    if (Test-Path $VsCMake) {
        $CMakeExe = $VsCMake
    } else {
        # Fallback to vswhere
        $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
        if (Test-Path $vswhere) {
            $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
            if ($vsPath) {
                $candidate = Join-Path $vsPath "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
                if (Test-Path $candidate) { $CMakeExe = $candidate }
            }
        }
    }
}

Write-Host "[+] Using CMake: $CMakeExe" -ForegroundColor Green

# -----------------------------------------------------------------------------
# 2. Build Windows Targets (x64 and ARM64)
# -----------------------------------------------------------------------------
if (-not $SkipWindows) {
    Write-Host "`n>>> [1/4] Building Windows x64 Release..." -ForegroundColor Yellow
    & $CMakeExe -B "$RepoRoot\build\win-x64" -A x64 -DVOLCANSTORAGE_BUILD_SAMPLES=ON
    & $CMakeExe --build "$RepoRoot\build\win-x64" --config Release
    & $CMakeExe --install "$RepoRoot\build\win-x64" --config Release --prefix "$RepoRoot\build\staging\win-x64"

    Write-Host "`n>>> [2/4] Building Windows ARM64 Release..." -ForegroundColor Yellow
    & $CMakeExe -B "$RepoRoot\build\win-arm64" -A ARM64 -DVOLCANSTORAGE_BUILD_SAMPLES=ON
    & $CMakeExe --build "$RepoRoot\build\win-arm64" --config Release
    & $CMakeExe --install "$RepoRoot\build\win-arm64" --config Release --prefix "$RepoRoot\build\staging\win-arm64"
}

# -----------------------------------------------------------------------------
# 3. Build Linux Targets via WSL (x64 and ARM64)
# -----------------------------------------------------------------------------
if (-not $SkipLinux) {
    if (Get-Command "wsl" -ErrorAction SilentlyContinue) {
        Write-Host "`n>>> [3/4] Building Linux x64 in WSL..." -ForegroundColor Yellow
        wsl bash -c "cd /mnt/c/Users/aziml/source/repos/VolcanStorage && cmake -B build/linux-x64 -DCMAKE_BUILD_TYPE=Release -DVOLCANSTORAGE_BUILD_SAMPLES=ON && cmake --build build/linux-x64 -j4 && cmake --install build/linux-x64 --prefix build/staging/linux-x64"

        Write-Host "`n>>> [4/4] Cross-compiling Linux ARM64 in WSL..." -ForegroundColor Yellow
        wsl bash -c "cd /mnt/c/Users/aziml/source/repos/VolcanStorage && cmake -B build/linux-arm64 -DCMAKE_BUILD_TYPE=Release -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 -DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc -DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++ -DVulkan_LIBRARY=/usr/lib/aarch64-linux-gnu/libvulkan.so -DVulkan_INCLUDE_DIR=/usr/include -DVOLCANSTORAGE_BUILD_SAMPLES=ON && cmake --build build/linux-arm64 -j4 && cmake --install build/linux-arm64 --prefix build/staging/linux-arm64"
    } else {
        Write-Warning "WSL not found. Skipping Linux compilation."
    }
}

# -----------------------------------------------------------------------------
# 4. Packaging with Zstandard-22 Compression
# -----------------------------------------------------------------------------
if (-not $SkipPackaging) {
    Write-Host "`n>>> Packaging distributions with Zstandard Level 22 into dist/..." -ForegroundColor Magenta
    wsl python3 /mnt/c/Users/aziml/source/repos/VolcanStorage/scripts/package_all.py
}

Write-Host "`n[SUCCESS] Multiplatform build and packaging completed!" -ForegroundColor Green
