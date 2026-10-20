# Shared helpers for the Ymir UWP scripts. Dot-source it: . (Join-Path $PSScriptRoot 'common.ps1')

$YmirRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path

<#
.SYNOPSIS
    Picks the Visual Studio version used for UWP builds and returns the matching CMake generator, configure preset and
    build directory.
.PARAMETER VisualStudio
    'auto' (default) prefers Visual Studio 2026 and falls back to 2022. The YMIR_UWP_VS environment variable
    ('2026' or '2022') overrides 'auto'.
#>
function Resolve-YmirUwpToolchain {
    param([ValidateSet('auto', '2026', '2022')][string]$VisualStudio = 'auto')

    if ($VisualStudio -eq 'auto' -and $env:YMIR_UWP_VS) { $VisualStudio = $env:YMIR_UWP_VS }

    if ($VisualStudio -eq 'auto') {
        $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
        if (!(Test-Path $vswhere)) { throw "vswhere.exe not found; install Visual Studio 2026 or 2022" }
        $hasVersion = {
            param($range)
            $path = & $vswhere -products * -version $range -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
                -property installationPath | Select-Object -First 1
            return [bool]$path
        }
        # The Visual Studio 2026 generator needs a recent CMake (4.2+); older CMake versions only know VS 2022
        $cmakeHasVS2026 = [bool]((cmake --help) -match 'Visual Studio 18 2026')
        if ($cmakeHasVS2026 -and (& $hasVersion '[18.0,19.0)')) { $VisualStudio = '2026' }
        elseif (& $hasVersion '[17.0,18.0)') { $VisualStudio = '2022' }
        elseif (& $hasVersion '[18.0,19.0)') { throw 'Visual Studio 2026 found, but this CMake does not support it (needs CMake 4.2+)' }
        else { throw 'No Visual Studio 2026 or 2022 installation with the C++ x64 tools was found' }
    }

    if ($VisualStudio -eq '2026') {
        $toolchain = [pscustomobject]@{
            VisualStudio = '2026'
            Generator    = 'Visual Studio 18 2026'
            Preset       = 'UWP-x86-64-AVX2-MSVC'
        }
    } else {
        $toolchain = [pscustomobject]@{
            VisualStudio = '2022'
            Generator    = 'Visual Studio 17 2022'
            Preset       = 'UWP-x86-64-AVX2-MSVC-VS2022'
        }
    }
    $toolchain | Add-Member -NotePropertyName BuildDir -NotePropertyValue (Join-Path $YmirRoot "out\build\$($toolchain.Preset)")
    $toolchain | Add-Member -NotePropertyName DepsBuildDir -NotePropertyValue (Join-Path $YmirRoot "out\uwp-deps-build\vs$($toolchain.VisualStudio)")
    return $toolchain
}

<#
.SYNOPSIS
    Maps the script-level configuration name to the CMake configuration name.
#>
function Get-YmirUwpCMakeConfig {
    param([ValidateSet('Debug', 'Release')][string]$Configuration)
    if ($Configuration -eq 'Debug') { return 'Debug' } else { return 'RelWithDebInfo' }
}

<#
.SYNOPSIS
    Finds the newest MSBuild package output folder (...\AppPackages\ymir-sdl3\ymir-sdl3_<ver>_x64_<Config>_Test) across
    all UWP build directories. Returns an object with Dir, Msix, Version, Dependencies (array) and Certificate.
#>
function Find-YmirUwpPackage {
    param([ValidateSet('Debug', 'Release')][string]$Configuration)

    $cmakeConfig = Get-YmirUwpCMakeConfig $Configuration
    $roots = Get-ChildItem (Join-Path $YmirRoot 'out\build') -Directory -Filter 'UWP-*' -ErrorAction SilentlyContinue |
        ForEach-Object { Join-Path $_.FullName 'apps\ymir-sdl3\AppPackages\ymir-sdl3' } |
        Where-Object { Test-Path $_ }
    $pkgDir = $roots | ForEach-Object {
        Get-ChildItem $_ -Directory -Filter "ymir-sdl3_*_x64_${cmakeConfig}_Test" -ErrorAction SilentlyContinue
    } | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (!$pkgDir) { return $null }

    $msix = Get-ChildItem $pkgDir.FullName -Filter '*.msix' | Select-Object -First 1
    if (!$msix) { return $null }
    $version = if ($msix.Name -match '_(\d+\.\d+\.\d+\.\d+)_') { $Matches[1] } else { '0.0.0.0' }

    return [pscustomobject]@{
        Dir          = $pkgDir.FullName
        Msix         = $msix
        Version      = $version
        Dependencies = @(Get-ChildItem (Join-Path $pkgDir.FullName 'Dependencies\x64') -Filter '*.appx' -ErrorAction SilentlyContinue)
        Certificate  = Get-ChildItem $pkgDir.FullName -Filter '*.cer' | Select-Object -First 1
        Symbols      = Get-ChildItem $pkgDir.FullName -Filter '*.appxsym' | Select-Object -First 1
    }
}
