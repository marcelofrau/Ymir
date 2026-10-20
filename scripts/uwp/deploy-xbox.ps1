<#
.SYNOPSIS
    Installs (and optionally launches) the Ymir UWP package on an Xbox in Dev Mode through the Device Portal REST API.

.DESCRIPTION
    Requires PowerShell 7+ (uses -Form and -SkipCertificateCheck).

    Connection settings come from scripts/uwp/xbox.env (gitignored). Copy xbox.env.example to xbox.env and fill in:
        XBOX_IP=192.168.0.50
        XBOX_USER=devportal-user
        XBOX_PASS=devportal-password

    The Device Portal listens on https://<XBOX_IP>:11443 with a self-signed TLS certificate. The username and
    password are the ones configured in Dev Home -> Remote Access settings.

    What the script does:
    1. Finds the newest .msix built by scripts/uwp/build.ps1 for the selected configuration.
    2. GET /api/os/info to obtain the CSRF-Token cookie (required for POST requests).
    3. POST /api/app/packagemanager/package?package=<msix name> with the .msix AND its dependency packages
       (Dependencies/x64/Microsoft.VCLibs*.appx) in the same multipart form.
    4. Polls GET /api/app/packagemanager/state until the installation finishes.
    5. With -Launch, starts the app through POST /api/taskmanager/app.

.PARAMETER Configuration
    Debug or Release; must match what was built.

.PARAMETER Launch
    Launch the app after installing it. Note that launching through the Device Portal is equivalent to launching
    from the tile (no debugger attached), which is the realistic test.

.EXAMPLE
    pwsh scripts/uwp/deploy-xbox.ps1 -Configuration Release -Launch
#>
param(
    [ValidateSet('Debug', 'Release')][string]$Configuration = 'Release',
    [switch]$Launch
)

$ErrorActionPreference = 'Stop'

if ($PSVersionTable.PSVersion.Major -lt 7) {
    throw 'This script requires PowerShell 7 or later (run it with pwsh).'
}

. (Join-Path $PSScriptRoot 'common.ps1')
$packageName = 'Ymir.SaturnEmulator.UWP' # must match Ymir_UWP_PACKAGE_NAME in apps/ymir-sdl3/uwp/UWPPackaging.cmake

# ── Load xbox.env ──
$envPath = Join-Path $PSScriptRoot 'xbox.env'
if (!(Test-Path $envPath)) { throw "Missing $envPath (copy xbox.env.example and fill it in)" }
$cfg = @{}
Get-Content $envPath | Where-Object { $_ -match '^\s*[^#].*=' } | ForEach-Object {
    $kv = $_ -split '=', 2
    $cfg[$kv[0].Trim()] = $kv[1].Trim()
}
foreach ($key in 'XBOX_IP', 'XBOX_USER', 'XBOX_PASS') {
    if (!$cfg[$key]) { throw "$envPath is missing $key" }
}
$baseUri = "https://$($cfg.XBOX_IP):11443"
$credential = [pscredential]::new($cfg.XBOX_USER, (ConvertTo-SecureString $cfg.XBOX_PASS -AsPlainText -Force))
$common = @{ Credential = $credential; Authentication = 'Basic'; SkipCertificateCheck = $true }

# ── Locate the package ──
$pkg = Find-YmirUwpPackage -Configuration $Configuration
if (!$pkg) { throw "No package found; run scripts/uwp/build.ps1 -Configuration $Configuration" }
$msix = $pkg.Msix
$deps = $pkg.Dependencies
Write-Host "Package:    $($msix.FullName)" -ForegroundColor Cyan
$deps | ForEach-Object { Write-Host "Dependency: $($_.FullName)" -ForegroundColor Cyan }

# ── CSRF token ──
Write-Host "=== Connecting to $baseUri ===" -ForegroundColor Cyan
$null = Invoke-RestMethod -Uri "$baseUri/api/os/info" -Method GET -SessionVariable session @common
$csrf = $session.Cookies.GetCookies($baseUri) | Where-Object Name -eq 'CSRF-Token' | Select-Object -First 1
$headers = @{}
if ($csrf) { $headers['X-CSRF-Token'] = $csrf.Value }

# ── Install ──
Write-Host '=== Uploading package ===' -ForegroundColor Cyan
$form = @{ $msix.Name = $msix }
foreach ($dep in $deps) { $form[$dep.Name] = $dep }
$installUri = "$baseUri/api/app/packagemanager/package?package=$([uri]::EscapeDataString($msix.Name))"
$null = Invoke-RestMethod -Uri $installUri -Method POST -Form $form -Headers $headers -WebSession $session @common

Write-Host '=== Waiting for installation ===' -ForegroundColor Cyan
$deadline = (Get-Date).AddMinutes(5)
while ($true) {
    Start-Sleep -Seconds 2
    $response = Invoke-WebRequest -Uri "$baseUri/api/app/packagemanager/state" -Method GET -Headers $headers `
        -WebSession $session -SkipHttpErrorCheck @common
    if ($response.StatusCode -eq 200) {
        $state = $response.Content | ConvertFrom-Json
        if ($state.Success -eq $false -or ($state.Code -and $state.Code -ne 0)) {
            throw "Installation failed: $($response.Content)"
        }
        break
    }
    if ($response.StatusCode -ne 204) {
        throw "Unexpected installation state response $($response.StatusCode): $($response.Content)"
    }
    if ((Get-Date) -gt $deadline) { throw 'Timed out waiting for the installation to finish' }
}
Write-Host 'Installed.' -ForegroundColor Green

if (!$Launch) { return }

# ── Launch ──
$packages = Invoke-RestMethod -Uri "$baseUri/api/app/packagemanager/packages" -Method GET -Headers $headers `
    -WebSession $session @common
$pkg = $packages.InstalledPackages | Where-Object { $_.PackageFullName -like "$packageName*" } | Select-Object -First 1
if (!$pkg) { throw "Installed package $packageName not found in the package list" }

$toBase64 = { param($s) [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($s)) }
$appId = & $toBase64 $pkg.PackageRelativeId
$fullName = & $toBase64 $pkg.PackageFullName
$launchUri = "$baseUri/api/taskmanager/app?appid=$([uri]::EscapeDataString($appId))&package=$([uri]::EscapeDataString($fullName))"
$null = Invoke-RestMethod -Uri $launchUri -Method POST -Headers $headers -WebSession $session @common
Write-Host "Launched $($pkg.PackageFullName)" -ForegroundColor Green
