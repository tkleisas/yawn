# build_yawn.ps1 — Build YAWN (Debug or Release)
# Usage: .\build_yawn.ps1 [-Config Release|Debug] [-Clean] [-Test]
#
# Optional deps (auto-detected if available):
#   - FFmpeg + libav*  → video clip import/playback
#   - libavdevice      → live video input (webcam)
#   - Ableton Link     → network beat/tempo sync (fetched automatically)
#   - VST3 SDK         → third-party plugin hosting (fetched automatically)
#
# CMake options (pass via -ExtraArgs):
#   -ExtraArgs "-DYAWN_VST3=OFF"       → disable VST3 plugin hosting
#   -ExtraArgs "-DYAWN_HAS_LINK=OFF"   → disable Ableton Link support
#   -ExtraArgs "-DYAWN_HAS_VIDEO=OFF"  → disable video support

param(
    [ValidateSet("Release","Debug")]
    [string]$Config = "Release",
    [switch]$Clean,
    [switch]$Test,
    [string]$ExtraArgs = ""
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

$ConfigCache = Join-Path $BuildDir "CMakeCache.txt"

if ($Clean -or -not (Test-Path $ConfigCache)) {
    if ($Clean) {
        Write-Host "-- Cleaning build directory..."
        Remove-Item -Recurse -Force $BuildDir -ErrorAction SilentlyContinue
    }
    Write-Host "-- Configuring CMake..."
    & $cmake -S $ScriptDir -B $BuildDir -DCMAKE_BUILD_TYPE=$Config $ExtraArgs.Split(' ')
    if ($LASTEXITCODE -ne 0) { throw "CMake configure failed" }
}

& $cmake --build $BuildDir --config $Config --parallel
if ($LASTEXITCODE -ne 0) { throw "Build failed" }

Write-Host ""
Write-Host "=== Build complete ===" -ForegroundColor Green
Write-Host "Executable: $BuildDir\bin\$Config\YAWN.exe"
$TestExe = Join-Path $BuildDir "bin" $Config "yawn_tests.exe"
if (Test-Path $TestExe) {
    Write-Host "Tests:      $TestExe"
}

if ($Test) {
    Write-Host ""
    Write-Host "=== Running tests ===" -ForegroundColor Cyan
    Push-Location $BuildDir
    ctest --output-on-failure -C $Config --parallel
    Pop-Location
}
