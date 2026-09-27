# Builds the native co-op proxy DLL (x64 Release) -> build\steam_api64.dll.
# Requires VS 2022 Build Tools with the C++ workload. The Steamworks SDK public
# headers are only needed once networking code is compiled in.
#
# Usage: powershell -File build.ps1 [-SteamSdkPublicDir path\to\sdk\public]

param(
    [string]$SteamSdkPublicDir = "E:\Ai Coding Projects\Steam SDK\steamworks_sdk_165\sdk\public"
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildDir = Join-Path $scriptDir "build"
New-Item -ItemType Directory -Force -Path $buildDir | Out-Null

# vcvars64.bat locates the VS install via vswhere.exe, which isn't on PATH in
# every shell.
$env:PATH += ";C:\Program Files (x86)\Microsoft Visual Studio\Installer"
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if (-not (Test-Path $vcvars)) {
    throw "vcvars64.bat not found at $vcvars -- is VS 2022 Build Tools installed?"
}

$forwardDef = Join-Path $scriptDir "steam_api64_proxy.def"
$importsDef = Join-Path $scriptDir "steam_api64_proxy_orig_imports.def"
$origLib = Join-Path $buildDir "steam_api64_orig.lib"
$outDll = Join-Path $buildDir "steam_api64.dll"

$sourceFiles = @(
    "dllmain.cpp", "mod_log.cpp", "config.cpp", "hooks.cpp", "game_chain.cpp",
    "local_input.cpp", "anm_tables.cpp", "player2.cpp", "coop_rules.cpp", "player_look.cpp", "settings_panel.cpp",
    "sim_control.cpp", "snapshot.cpp", "checksum.cpp", "transport.cpp", "lobby.cpp", "netplay.cpp",
    "overlay_renderer.cpp", "text_renderer.cpp", "status_overlay.cpp",
    "third_party\minhook\src\buffer.c",
    "third_party\minhook\src\hook.c",
    "third_party\minhook\src\trampoline.c",
    "third_party\minhook\src\hde\hde64.c"
)

$includes = "/I `"$(Join-Path $scriptDir 'third_party\minhook\include')`""
if (Test-Path $SteamSdkPublicDir) {
    $includes += " /I `"$SteamSdkPublicDir`""
}

# The forwarder .def only produces true cross-module forwards if the linker
# can resolve steam_api64_orig against an import library (overlay/13).
Write-Host "Generating import library for steam_api64_orig.dll..."
cmd /c "`"$vcvars`" >nul && lib.exe /nologo /DEF:`"$importsDef`" /OUT:`"$origLib`" /MACHINE:X64"
if ($LASTEXITCODE -ne 0) { throw "lib.exe failed (exit code $LASTEXITCODE)" }

$objFiles = @()
foreach ($src in $sourceFiles) {
    $srcPath = Join-Path $scriptDir $src
    $objPath = Join-Path $buildDir ([System.IO.Path]::GetFileNameWithoutExtension($src) + ".obj")
    $warn = if ($src.StartsWith("third_party")) { "/W3" } else { "/W4" }
    Write-Host "Compiling $src..."
    cmd /c "`"$vcvars`" >nul && cl.exe /nologo /c /EHsc /O2 $warn /std:c++17 /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS $includes /Fo:`"$objPath`" `"$srcPath`""
    if ($LASTEXITCODE -ne 0) { throw "Compile failed for $src (exit code $LASTEXITCODE)" }
    $objFiles += "`"$objPath`""
}

Write-Host "Linking..."
cmd /c "`"$vcvars`" >nul && link.exe /nologo /DLL /DEF:`"$forwardDef`" $($objFiles -join ' ') `"$origLib`" user32.lib /OUT:`"$outDll`""
if ($LASTEXITCODE -ne 0) { throw "Link failed (exit code $LASTEXITCODE)" }

Write-Host "`nBuilt: $outDll"
