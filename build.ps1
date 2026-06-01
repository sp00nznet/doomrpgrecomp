# build.ps1 -- one-shot recompile + native build for doomrpgrecomp (MSVC + SDL2).
#
#   .\build.ps1            # regenerate C, compile, link, stage assets+DLL
#   .\build.ps1 -Run       # ...then launch it
#
# Requires: Visual Studio 2022, Python 3, and SDL2 via vcpkg at $VcpkgRoot.
param([switch]$Run, [string]$VcpkgRoot = "C:\vcpkg", [int]$Scale = 5)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Set-Location $root

$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$sdlInc = "$VcpkgRoot\installed\x64-windows\include\SDL2"
$imguiInc = "$VcpkgRoot\installed\x64-windows\include"        # imgui.h
$beInc = "runtime\third_party\imgui_backends"                  # vendored SDL2 backends
$sdlLib = "$VcpkgRoot\installed\x64-windows\lib"
$sdlDll = "$VcpkgRoot\installed\x64-windows\bin\SDL2.dll"

New-Item -ItemType Directory -Force build\obj | Out-Null

Write-Host "[1/4] Recompiling bytecode -> C ..."
python tools\recompiler\jrecomp.py translate game\DoomRPG.jar -o generated\
if ($LASTEXITCODE -ne 0) { throw "recompiler failed" }

Write-Host "[2/4] Compiling C (generated + runtime) ..."
# /MD on everything so the C objects match vcpkg's dynamic-CRT imgui.lib.
$compile = "`"$vcvars`" >nul 2>&1 && cl /nologo /c /O2 /MD /W3 /wd4102 /I runtime\include /I generated /I `"$sdlInc`" /Fobuild\obj\ generated\*.c runtime\src\*.c"
cmd /c $compile
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

Write-Host "[2b/4] Compiling C++ (ImGui dev menu + backends) ..."
$compilecpp = "`"$vcvars`" >nul 2>&1 && cl /nologo /c /O2 /MD /EHsc /W3 /I runtime\include /I generated /I `"$imguiInc`" /I `"$beInc`" /I `"$sdlInc`" /Fobuild\obj\ runtime\src\devgui.cpp $beInc\*.cpp"
cmd /c $compilecpp
if ($LASTEXITCODE -ne 0) { throw "C++ compile failed" }

Write-Host "[3/4] Linking DoomRPG.exe ..."
$link = "`"$vcvars`" >nul 2>&1 && link /nologo /SUBSYSTEM:CONSOLE /OUT:build\DoomRPG.exe build\obj\*.obj `"$sdlLib\SDL2.lib`" `"$sdlLib\imgui.lib`" winmm.lib"
cmd /c $link
if ($LASTEXITCODE -ne 0) { throw "link failed" }

Write-Host "[4/4] Staging assets + SDL2.dll ..."
New-Item -ItemType Directory -Force game\extracted | Out-Null
if (-not (Test-Path game\extracted\DoomRPG.class)) {
    & "$env:JAVA_HOME\bin\jar.exe" xf (Resolve-Path game\DoomRPG.jar) -C game\extracted 2>$null
    if (-not (Test-Path game\extracted\DoomRPG.class)) {
        Expand-Archive -Path game\DoomRPG.jar -DestinationPath game\extracted -Force -ErrorAction SilentlyContinue
    }
}
Copy-Item $sdlDll build\ -Force

Write-Host "Done -> build\DoomRPG.exe" -ForegroundColor Green
if ($Run) {
    & build\DoomRPG.exe (Resolve-Path game\extracted) $Scale
}
