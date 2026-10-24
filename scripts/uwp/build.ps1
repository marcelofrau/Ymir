<#
.SYNOPSIS
    Configures and builds the Ymir UWP package (.msix) for Xbox Dev Mode.

.DESCRIPTION
    1. Builds the prebuilt dependencies (SDL3-uwp) if out/uwp-deps is missing.
    2. Configures the UWP CMake preset (CMAKE_SYSTEM_NAME=WindowsStore) for the detected Visual Studio version:
       UWP-x86-64-AVX2-MSVC (VS 2026) or UWP-x86-64-AVX2-MSVC-VS2022 (VS 2022).
    3. Builds the selected configuration. MSBuild produces a signed .msix under
       out/build/<preset>/apps/ymir-sdl3/AppPackages/ymir-sdl3/ymir-sdl3_<version>_x64_<Config>_Test/.
    4. Prints the path of the .msix and of its dependency packages (Microsoft.VCLibs).

.PARAMETER Configuration
    Debug or Release (Release maps to the RelWithDebInfo CMake configuration).

.PARAMETER VisualStudio
    auto (default: 2026 if installed, otherwise 2022), 2026 or 2022.

.PARAMETER PackageVersion
    Optional four-part package version (e.g. 0.4.0.12). Defaults to <Ymir version>.0.

.PARAMETER Reconfigure
    Re-run the CMake configure step with --fresh. Does not delete anything.

.EXAMPLE
    pwsh scripts/uwp/build.ps1 -Configuration Release
#>
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [ValidateSet('auto', '2026', '2022')][string]$VisualStudio = 'auto',
    [string]$PackageVersion = '',
    [switch]$Reconfigure
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'common.ps1')

$toolchain = Resolve-YmirUwpToolchain -VisualStudio $VisualStudio
$cmakeConfig = Get-YmirUwpCMakeConfig $Configuration
Write-Host "Using Visual Studio $($toolchain.VisualStudio), preset $($toolchain.Preset)" -ForegroundColor Cyan

Push-Location $YmirRoot
try {
    if (!(Test-Path (Join-Path $YmirRoot 'out\uwp-deps\bin\SDL3.dll'))) {
        & (Join-Path $PSScriptRoot 'build-deps.ps1') -VisualStudio $toolchain.VisualStudio
    }

    if (!(Test-Path (Join-Path $YmirRoot 'vcpkg\vcpkg.exe'))) {
        Write-Host '=== Bootstrapping vcpkg ===' -ForegroundColor Cyan
        & (Join-Path $YmirRoot 'vcpkg\bootstrap-vcpkg.bat') -disableMetrics
        if ($LASTEXITCODE -ne 0) { throw 'vcpkg bootstrap failed' }
    }

    Write-Host "=== Configuring $($toolchain.Preset) ===" -ForegroundColor Cyan
    $devBuild = if ($Configuration -eq 'Debug') { 'ON' } else { 'OFF' }
    $configureArgs = @(
        '--preset', $toolchain.Preset,
        "-DYmir_UWP_PACKAGE_VERSION=$PackageVersion",
        "-DYmir_DEV_BUILD=$devBuild"
    )
    if ($Reconfigure) { $configureArgs += '--fresh' }
    cmake @configureArgs
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }

    Write-Host "=== Building ymir-sdl3 ($cmakeConfig) ===" -ForegroundColor Cyan
    cmake --build $toolchain.BuildDir --config $cmakeConfig --target ymir-sdl3 -- '/m' '/v:minimal'
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }

    $pkg = Find-YmirUwpPackage -Configuration $Configuration
    if (!$pkg) { throw "No .msix found under $($toolchain.BuildDir)" }

    Write-Host ''
    Write-Host "MSIX:         $($pkg.Msix.FullName)" -ForegroundColor Green
    $pkg.Dependencies | ForEach-Object { Write-Host "Dependency:   $($_.FullName)" -ForegroundColor Green }
    if ($pkg.Certificate) { Write-Host "Certificate:  $($pkg.Certificate.FullName)" -ForegroundColor Green }
} finally {
    Pop-Location
}
