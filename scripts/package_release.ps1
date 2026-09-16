# VolcanStorage Universal Packaging Script
param(
    [string]$Platform = "windows", # windows | linux
    [string]$Arch = "x64",         # x64 | arm64
    [string]$Config = "Release",
    [switch]$All
)

$ErrorActionPreference = "Stop"

function Package-Target([string]$p, [string]$a, [string]$buildPath, [string]$binExt, [string]$libPrefix, [string]$libExt) {
    $pCap = if ($p -eq "windows") { "Windows" } else { "Linux" }
    $distDir = "dist\staging_${p}_${a}"
    $zipPath = "dist\VolcanStorage-${pCap}-${a}.zip"

    Write-Host "[VolcanStorage] Packaging ${pCap} ${a} from ${buildPath}..." -ForegroundColor Cyan

    if (Test-Path $distDir) { Remove-Item -Recurse -Force $distDir }
    New-Item -ItemType Directory -Path "$distDir\bin" -Force | Out-Null
    New-Item -ItemType Directory -Path "$distDir\include\volcanstorage" -Force | Out-Null
    New-Item -ItemType Directory -Path "$distDir\docs" -Force | Out-Null

    if ($p -eq "windows") {
        Copy-Item "$buildPath\src\$Config\VolcanStorage.lib" "$distDir\bin\" -ErrorAction SilentlyContinue
        Copy-Item "$buildPath\samples\HelloVolcanStorage\$Config\HelloVolcanStorage.exe" "$distDir\bin\" -ErrorAction SilentlyContinue
        Copy-Item "$buildPath\GDeflate\GDeflate\$Config\GDeflate.lib" "$distDir\bin\" -ErrorAction SilentlyContinue
        Copy-Item "$buildPath\GDeflate\GDeflate\$Config\deflate.lib" "$distDir\bin\" -ErrorAction SilentlyContinue
    } else {
        Copy-Item "$buildPath/src/libVolcanStorage.a" "$distDir\bin\" -ErrorAction SilentlyContinue
        Copy-Item "$buildPath/samples/HelloVolcanStorage/HelloVolcanStorage" "$distDir\bin\" -ErrorAction SilentlyContinue
        Copy-Item "$buildPath/GDeflate/GDeflate/libGDeflate.a" "$distDir\bin\" -ErrorAction SilentlyContinue
        Copy-Item "$buildPath/GDeflate/GDeflate/libdeflate.a" "$distDir\bin\" -ErrorAction SilentlyContinue
    }

    if (Test-Path "shaders\GDeflate.spv") {
        Copy-Item "shaders\GDeflate.spv" "$distDir\bin\"
    }

    Copy-Item "include\volcanstorage\volcanstorage.h" "$distDir\include\volcanstorage\"
    if (Test-Path "Docs") {
        New-Item -ItemType Directory -Force -Path "$distDir\docs" | Out-Null
        Copy-Item "Docs\VolcanStorage_Developer_Guide.pdf" "$distDir\docs\" -ErrorAction SilentlyContinue
        Copy-Item "Docs\VolcanStorage_Developer_Guide.html" "$distDir\docs\" -ErrorAction SilentlyContinue
    }
    Copy-Item "LICENSE" "$distDir\"
    Copy-Item "NOTICES.txt" "$distDir\"
    Copy-Item "README.md" "$distDir\"

    Compress-Archive -Path "$distDir\*" -DestinationPath $zipPath -Force
    Remove-Item -Recurse -Force $distDir
    Write-Host "[VolcanStorage] Generated: $zipPath" -ForegroundColor Green
}

if ($All) {
    if (Test-Path "build") { Package-Target "windows" "x64" "build" ".exe" "" ".lib" }
    if (Test-Path "build_win_arm64") { Package-Target "windows" "arm64" "build_win_arm64" ".exe" "" ".lib" }
    if (Test-Path "build_linux_x64") { Package-Target "linux" "x64" "build_linux_x64" "" "lib" ".a" }
    if (Test-Path "build_linux_arm64") { Package-Target "linux" "arm64" "build_linux_arm64" "" "lib" ".a" }
} else {
    $buildDir = if ($Platform -eq "windows") {
        if ($Arch -eq "arm64") { "build_win_arm64" } else { "build" }
    } else {
        if ($Arch -eq "arm64") { "build_linux_arm64" } else { "build_linux_x64" }
    }
    Package-Target $Platform $Arch $buildDir
}
