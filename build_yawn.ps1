# build_yawn.ps1 — Build YAWN (Debug or Release)
# Usage: .\build_yawn.ps1 [-Config Release|Debug]

param(
    [ValidateSet("Release","Debug")]
    [string]$Config = "Release"
)

$ErrorActionPreference = "Stop"
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$BuildDir  = Join-Path $ScriptDir "build"

# Locate CMake (prefer VS bundled, fall back to PATH)
$VsCmake = "C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if (Test-Path $VsCmake) {
    $cmake = $VsCmake
} else {
    $cmake = "cmake"
}

Write-Host "=== Building YAWN ($Config) ===" -ForegroundColor Cyan

if (-not (Test-Path $BuildDir)) {
    Write-Host "-- Configuring CMake..."
    & $cmake -S $ScriptDir -B $BuildDir -DCMAKE_BUILD_TYPE=$Config
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

& $cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host ""
Write-Host "=== Build complete ===" -ForegroundColor Green
Write-Host "Executable: $BuildDir\bin\$Config\YAWN.exe"
Write-Host "Tests:      $BuildDir\bin\$Config\yawn_tests.exe"
