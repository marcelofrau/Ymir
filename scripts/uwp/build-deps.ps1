<#
.SYNOPSIS
    Builds the prebuilt UWP dependencies of Ymir into out/uwp-deps.

.DESCRIPTION
    Currently this is SDL3-uwp (git submodule vendor/SDL3-uwp, SternXD's fork of SDL with the WinRT backend restored).
    It is built as a DLL with CMake for WindowsStore and installed into out/uwp-deps, which the UWP CMake preset adds
    to CMAKE_PREFIX_PATH. The DLL is then shipped inside the .msix by apps/ymir-sdl3/uwp/UWPPackaging.cmake.

    Run this once after cloning, and again whenever the vendor/SDL3-uwp submodule is updated.

.PARAMETER Generator
    CMake generator to use. Defaults to Visual Studio 2026 (v145 toolset), which is what the UWP preset uses.

.PARAMETER Clean
    Reconfigure SDL3-uwp from scratch (uses cmake --fresh). Does not delete anything.

.EXAMPLE
    pwsh scripts/uwp/build-deps.ps1
#>
param(
    [string]$Generator = 'Visual Studio 18 2026',
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$sdlSource = Join-Path $root 'vendor\SDL3-uwp'
$sdlBuild = Join-Path $root 'out\uwp-deps-build\SDL3'
$prefix = Join-Path $root 'out\uwp-deps'

if (!(Test-Path (Join-Path $sdlSource 'CMakeLists.txt'))) {
    Write-Host 'vendor/SDL3-uwp is empty; initializing submodules...' -ForegroundColor Yellow
    git -C $root submodule update --init --recursive
    if ($LASTEXITCODE -ne 0) { throw 'git submodule update failed' }
}

$configureArgs = @(
    '-S', $sdlSource,
    '-B', $sdlBuild,
    '-G', $Generator,
    '-A', 'x64',
    '-DCMAKE_SYSTEM_NAME=WindowsStore',
    '-DCMAKE_SYSTEM_VERSION=10.0',
    "-DCMAKE_INSTALL_PREFIX=$prefix",
    '-DSDL_SHARED=ON',
    '-DSDL_STATIC=OFF',
    '-DSDL_TEST_LIBRARY=OFF',
    '-DSDL_TESTS=OFF',
    '-DSDL_EXAMPLES=OFF'
)
if ($Clean) { $configureArgs += '--fresh' }

Write-Host '=== Configuring SDL3-uwp ===' -ForegroundColor Cyan
cmake @configureArgs
if ($LASTEXITCODE -ne 0) { throw 'SDL3-uwp configure failed' }

Write-Host '=== Building and installing SDL3-uwp (Release) ===' -ForegroundColor Cyan
cmake --build $sdlBuild --config Release --target install -- '/m' '/v:minimal'
if ($LASTEXITCODE -ne 0) { throw 'SDL3-uwp build failed' }

$dll = Join-Path $prefix 'bin\SDL3.dll'
if (!(Test-Path $dll)) { throw "SDL3.dll not found at $dll" }
Write-Host "SDL3-uwp installed: $dll" -ForegroundColor Green
