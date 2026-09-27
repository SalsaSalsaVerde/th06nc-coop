# Installs build\steam_api64.dll into the th06nc game directory, or swaps back
# to whatever proxy was installed before (e.g. the overlay co-op mod).
#
#   powershell -File deploy.ps1            # install this mod
#   powershell -File deploy.ps1 -Restore   # put the previous proxy back
#
# The game directory must already have the proxy layout: the real Steam DLL
# renamed to steam_api64_orig.dll (the overlay mod's installer sets this up).

param(
    [string]$GameDir = "E:\SteamLibrary\steamapps\common\th06nc",
    [switch]$Restore
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$built = Join-Path $scriptDir "build\steam_api64.dll"
$installed = Join-Path $GameDir "steam_api64.dll"
$original = Join-Path $GameDir "steam_api64_orig.dll"
$backup = Join-Path $GameDir "steam_api64.previous_proxy.dll"

if (-not (Test-Path (Join-Path $GameDir "th06nc.exe"))) {
    throw "th06nc.exe not found in $GameDir -- wrong directory (th06c is not supported)."
}
if (-not (Test-Path $original)) {
    throw "steam_api64_orig.dll missing in $GameDir -- install the overlay mod once first so the real DLL is renamed."
}

if ($Restore) {
    if (-not (Test-Path $backup)) { throw "No backup at $backup -- nothing to restore." }
    Copy-Item $backup $installed -Force
    Write-Host "Restored previous proxy from $backup"
    return
}

if (-not (Test-Path $built)) { throw "Build output missing: $built -- run build.ps1 first." }

if (Test-Path $installed) {
    $installedHash = (Get-FileHash $installed).Hash
    $builtHash = (Get-FileHash $built).Hash
    if ($installedHash -eq $builtHash) {
        Write-Host "Already installed (identical)."
        return
    }
    $isOurs = Select-String -Path $installed -Pattern "th06nc_native_coop" -SimpleMatch -Quiet
    if (-not $isOurs) {
        Copy-Item $installed $backup -Force
        Write-Host "Backed up the currently installed proxy to $backup"
    }
}

Copy-Item $built $installed -Force
Write-Host "Installed $built -> $installed"
Write-Host "Log: $(Join-Path $GameDir 'th06nc_native_coop.log')"
