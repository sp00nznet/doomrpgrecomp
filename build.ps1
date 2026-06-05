# build.ps1 -- one-shot recompile + native build for doomrpgrecomp (MSVC + SDL2).
#
#   .\build.ps1                       # build the default game (Doom RPG)
#   .\build.ps1 -Run                  # ...then launch it
#   .\build.ps1 -Game DoomIIRPG -Run  # build+run another game from the registry
#   .\build.ps1 -List                 # list the games in tools\games.json
#
# Each game is its own build (the obfuscated class names collide across games):
# generated\<exe>\, build\<exe>\obj\ -> build\<exe>.exe, assets in
# game\extracted\<exe>\. See tools\games.json for the registry.
#
# Requires: Visual Studio 2022, Python 3, and SDL2 via vcpkg at $VcpkgRoot.
param([switch]$Run, [switch]$List, [string]$Game = "", [string]$VcpkgRoot = "C:\vcpkg", [int]$Scale = 0)

$ErrorActionPreference = "Stop"
$root = $PSScriptRoot
Set-Location $root

# ---- game registry --------------------------------------------------------
$registry = Get-Content (Join-Path $root "tools\games.json") -Raw | ConvertFrom-Json
if ($List) {
    Write-Host "Games in tools\games.json (default: $($registry.default)):"
    foreach ($p in $registry.games.PSObject.Properties) {
        $g = $p.Value
        Write-Host ("  {0,-14} {1,-18} {2}x{3}  <- {4}" -f $p.Name, $g.name, $g.screenW, $g.screenH, $g.jar)
    }
    return
}
if (-not $Game) { $Game = $registry.default }
$g = $registry.games.$Game
if (-not $g) { throw "unknown game '$Game'. Run .\build.ps1 -List to see the registry." }
$jar = Join-Path $root $g.jar
if (-not (Test-Path $jar)) { throw "JAR not found: $jar (bring your own legally-obtained copy)" }
$exe = $g.exe
$screen = "$($g.screenW)x$($g.screenH)"
if ($Scale -le 0) { $Scale = if ($g.screenW -le 160) { 5 } else { 2 } }

# ---- paths ----------------------------------------------------------------
$gen     = Join-Path $root "generated\$exe"
$objdir  = Join-Path $root "build\$exe\obj"
$exepath = Join-Path $root "build\$exe.exe"
$extract = Join-Path $root "game\extracted\$exe"

$vcvars = "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
$sdlInc = "$VcpkgRoot\installed\x64-windows\include\SDL2"
$imguiInc = "$VcpkgRoot\installed\x64-windows\include"        # imgui.h
$beInc = "runtime\third_party\imgui_backends"                  # vendored SDL2 backends
$sdlLib = "$VcpkgRoot\installed\x64-windows\lib"
$sdlDll = "$VcpkgRoot\installed\x64-windows\bin\SDL2.dll"

# Fresh per-game obj dir so stale objects from a prior build never get linked.
if (Test-Path $objdir) { Remove-Item -Recurse -Force $objdir }
New-Item -ItemType Directory -Force $objdir | Out-Null
New-Item -ItemType Directory -Force $gen | Out-Null

Write-Host "[1/4] Recompiling $($g.name) bytecode -> C  ($screen) ..."
python tools\recompiler\jrecomp.py translate $jar -o $gen --screen $screen --name $g.name
if ($LASTEXITCODE -ne 0) { throw "recompiler failed" }
# Emit the savestate registry of every generated static global.
python tools\gen_savestate.py "$gen\doomrpg.h" "$gen\savestate_registry.c"
if ($LASTEXITCODE -ne 0) { throw "gen_savestate failed" }

Write-Host "[2/4] Compiling C (generated + runtime) ..."
# Per-game dev-menu profile (cheats binding); falls back to the stub.
$profileName = if ($g.profile) { $g.profile } else { "stub" }
$profileSrc = Join-Path $root "runtime\src\profiles\profile_$profileName.c"
if (-not (Test-Path $profileSrc)) {
    Write-Warning "no profile_$profileName.c for $Game; using stub (cheats degrade to the built-in debug menu)"
    $profileSrc = Join-Path $root "runtime\src\profiles\profile_stub.c"
}
# /MD on everything so the C objects match vcpkg's dynamic-CRT imgui.lib.
# runtime\src\*.c is non-recursive, so profiles\ is excluded; we add the one we want.
$compile = "`"$vcvars`" >nul 2>&1 && cl /nologo /c /O2 /Zi /MD /W3 /wd4102 /Fd`"$objdir\\`" /I runtime\include /I `"$gen`" /I `"$sdlInc`" /Fo`"$objdir\\`" `"$gen\*.c`" runtime\src\*.c `"$profileSrc`""
cmd /c $compile
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

Write-Host "[2b/4] Compiling C++ (ImGui dev menu + backends) ..."
$compilecpp = "`"$vcvars`" >nul 2>&1 && cl /nologo /c /O2 /Zi /MD /EHsc /W3 /Fd`"$objdir\\`" /I runtime\include /I `"$gen`" /I `"$imguiInc`" /I `"$beInc`" /I `"$sdlInc`" /Fo`"$objdir\\`" runtime\src\devgui.cpp $beInc\*.cpp"
cmd /c $compilecpp
if ($LASTEXITCODE -ne 0) { throw "C++ compile failed" }

Write-Host "[3/4] Linking $exe.exe ..."
# /STACK 256MB: the game's worker loop runs inline on the main thread, and level
# load / rendering recurse deeply -- the default 1MB stack overflows (intermittent
# crash that bypasses the SEH handler). Big reserve matches a J2ME thread's room.
# /DYNAMICBASE:NO pins the exe load address so save states (which snapshot raw
# cls pointers into the exe) reload across runs of the same build.
$link = "`"$vcvars`" >nul 2>&1 && link /nologo /DEBUG /MAP:`"build\$exe.map`" /STACK:0x10000000,0x100000 /DYNAMICBASE:NO /SUBSYSTEM:CONSOLE /OUT:`"$exepath`" `"$objdir\*.obj`" `"$sdlLib\SDL2.lib`" `"$sdlLib\imgui.lib`" winmm.lib ole32.lib dbghelp.lib"
cmd /c $link
if ($LASTEXITCODE -ne 0) { throw "link failed" }

Write-Host "[4/4] Staging assets + SDL2.dll ..."
New-Item -ItemType Directory -Force $extract | Out-Null
if (-not (Get-ChildItem $extract -Filter *.class -ErrorAction SilentlyContinue)) {
    # A JAR is a zip; Expand-Archive needs a .zip extension, so copy to a temp.
    $tmp = Join-Path $env:TEMP ("recomp_" + $exe + ".zip")
    Copy-Item (Resolve-Path $jar) $tmp -Force
    Expand-Archive -Path $tmp -DestinationPath $extract -Force
    Remove-Item $tmp -Force
}
# Non-fatal: the DLL may be locked by another game already running.
try { Copy-Item $sdlDll (Join-Path $root "build\") -Force -ErrorAction Stop }
catch { if (Test-Path (Join-Path $root "build\SDL2.dll")) { Write-Host "  (SDL2.dll in use; existing copy kept)" } else { throw } }

Write-Host "Done -> $exepath" -ForegroundColor Green
if ($Run) {
    & $exepath (Resolve-Path $extract) $Scale
}
