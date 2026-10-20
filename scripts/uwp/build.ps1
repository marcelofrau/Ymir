<#
.SYNOPSIS
    Configures and builds the Ymir UWP package (.msix) for Xbox Dev Mode.

.DESCRIPTION
    1. Builds the prebuilt dependencies (SDL3-uwp) if out/uwp-deps is missing.
    2. Configures the UWP-x86-64-AVX2-MSVC CMake preset (Visual Studio 2026 generator, CMAKE_SYSTEM_NAME=WindowsStore).
    3. Builds the selected configuration. MSBuild produces a signed .msix under
       out/build/UWP-x86-64-AVX2-MSVC/apps/ymir-sdl3/AppPackages/ymir-sdl3/.
    4. Prints the path of the .msix and of its dependency packages (Microsoft.VCLibs).

.PARAMETER Configuration
    Debug or Release (Release maps to the RelWithDebInfo CMake configuration).

.PARAMETER Reconfigure
    Re-run the CMake configure step with --fresh. Does not delete anything.

.EXAMPLE
    pwsh scripts/uwp/build.ps1 -Configuration Release
#>
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'

$root = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$preset = 'UWP-x86-64-AVX2-MSVC'
$buildDir = Join-Path $root "out\build\$preset"
$cmakeConfig = if ($Configuration -eq 'Debug') { 'Debug' } else { 'RelWithDebInfo' }

Push-Location $root
try {
    if (!(Test-Path (Join-Path $root 'out\uwp-deps\bin\SDL3.dll'))) {
        & (Join-Path $PSScriptRoot 'build-deps.ps1')
    }

    if (!(Test-Path (Join-Path $root 'vcpkg\vcpkg.exe'))) {
        Write-Host '=== Bootstrapping vcpkg ===' -ForegroundColor Cyan
        & (Join-Path $root 'vcpkg\bootstrap-vcpkg.bat') -disableMetrics
        if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed' }
    }

    Write-Host "=== Configuring $preset ===" -ForegroundColor Cyan
    $configureArgs = @('--preset', $preset)
    if ($Reconfigure) { $configureArgs += '--fresh' }
    cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

    Write-Host "=== Building ymir-sdl3 ($cmakeConfig) ===" -ForegroundColor Cyan
    cmake --build $buildDir --config $cmakeConfig --target ymir-sdl3 -- '/m' '/v:minimal'
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

    $pkgRoot = Join-Path $buildDir 'apps\ymir-sdl3\AppPackages\ymir-sdl3'
    $pkgDir = Get-ChildItem $pkgRoot -Directory -Filter "ymir-sdl3_*_x64_${cmakeConfig}_Test" |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (!$pkgDir) { throw "No package folder found under $pkgRoot" }
    $msix = Get-ChildItem $pkgDir.FullName -Filter '*.msix' | Select-Object -First 1
    if (!$msix) { throw "No .msix found in $($pkgDir.FullName)" }

    Write-Host ''
    Write-Host "MSIX:         $($msix.FullName)" -ForegroundColor Green
    Get-ChildItem (Join-Path $pkgDir.FullName 'Dependencies\x64') -Filter '*.appx' -ErrorAction SilentlyContinue |
        ForEach-Object { Write-Host "Dependency:   $($_.FullName)" -ForegroundColor Green }
    $cer = Get-ChildItem $pkgDir.FullName -Filter '*.cer' | Select-Object -First 1
    if ($cer) { Write-Host "Certificate:  $($cer.FullName)" -ForegroundColor Green }
} finally {
    Pop-Location
}
