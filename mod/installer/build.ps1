# Builds th06nc_native_coop_installer.exe: one self-contained installer with the
# mod DLL and the example settings embedded (docs/09). Build the mod first
# (..\build.ps1) -- this embeds whatever is in ..\build\steam_api64.dll.
#
# Usage: powershell -File build.ps1
# Output: build\th06nc_native_coop_installer.exe

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $scriptDir "build"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

$modDll = Join-Path $scriptDir "..\build\steam_api64.dll"
if (-not (Test-Path $modDll)) {
    throw "Mod DLL not found at $modDll -- build it first: powershell -File ..\build.ps1"
}

$env:PATH += ";C:\Program Files (x86)\Microsoft Visual Studio\Installer"
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat not found at $vcvars -- is VS 2022 Build Tools installed?"
}

$objPath = Join-Path $buildDir "installer.obj"
$resPath = Join-Path $buildDir "installer.res"
$outExe = Join-Path $buildDir "th06nc_native_coop_installer.exe"

Write-Host "Compiling installer.cpp..."
cmd /c "`"$vcvars`" >nul && cl.exe /nologo /c /EHsc /O2 /W4 /std:c++17 /D_CRT_SECURE_NO_WARNINGS /Fo:`"$objPath`" `"$scriptDir\installer.cpp`""
if ($LASTEXITCODE -ne 0) { throw "Compile failed (exit code $LASTEXITCODE)" }

Write-Host "Compiling installer.rc (embeds the mod DLL and example settings)..."
Push-Location $scriptDir
try {
    cmd /c "`"$vcvars`" >nul && rc.exe /nologo /fo `"$resPath`" installer.rc"
    if ($LASTEXITCODE -ne 0) { throw "Resource compile failed (exit code $LASTEXITCODE)" }
} finally {
    Pop-Location
}

Write-Host "Linking..."
cmd /c "`"$vcvars`" >nul && link.exe /nologo `"$objPath`" `"$resPath`" /OUT:`"$outExe`" /SUBSYSTEM:CONSOLE"
if ($LASTEXITCODE -ne 0) { throw "Link failed (exit code $LASTEXITCODE)" }

Write-Host "`nBuilt: $outExe"
