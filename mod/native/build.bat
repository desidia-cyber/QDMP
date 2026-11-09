@echo off
REM ---------------------------------------------------------------------------
REM Build winmm.dll -- the QD co-op native module.
REM
REM WHAT THIS PRODUCES
REM ------------------
REM A proxy DLL named after a system DLL the game actually imports, plus MinHook,
REM linked into one file.  It goes next to QueensDomain-Win64-Shipping.exe, and the
REM loader maps it before the entry point runs: no injector, no remote thread.
REM
REM WHY winmm.dll AND NOT version.dll OR dxgi.dll
REM ---------------------------------------------
REM Measured from the shipped image's import directory:
REM   * version.dll is NOT imported by QueensDomain-Win64-Shipping.exe, so a
REM     version.dll proxy would never be loaded.  (The previous project shipped one
REM     because Dolls Nest did import it.)
REM   * dxgi.dll IS imported, but this install carries vkd3d-proton.cache, so a
REM     D3D compatibility layer is or has been in play; shadowing dxgi would break
REM     rendering in a way that looks like a game bug.
REM   * winmm.dll IS imported, is not a KnownDLL, is graphics-free, and all 180 of
REM     its exports are forwarded (generated from the real DLL by
REM     tools/gen_proxy_def.py, so the list cannot drift).
REM There are no DLLs beside the shipping exe today, so there is nothing to
REM overwrite.
REM
REM NOTE ON STYLE: this file uses `goto` rather than parenthesised if-blocks.
REM `%ProgramFiles(x86)%` contains a ')' which closes such a block early -- that
REM bug silently skipped the environment setup and reported "vswhere not found" in
REM the previous project's build script.
REM ---------------------------------------------------------------------------
setlocal

set HERE=%~dp0
set ROOT=%HERE%..
set MH=%ROOT%\third_party\minhook
set SRC=%HERE%src
set OUT=%HERE%build

set "PF86=%ProgramFiles(x86)%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%SRC%\targets.h" goto :no_targets
if not exist "%SRC%\exports.h" goto :no_exports
if not exist "%SRC%\fwd_symbols.c" goto :no_cstubs
if not exist "%HERE%\proxy.def" goto :no_def
if not exist "%MH%\src\hook.c" goto :no_minhook

if defined VSCMD_ARG_TGT_ARCH goto :have_msvc
if not exist "%VSWHERE%" goto :no_vswhere

set "VSPATH="
REM vswhere's own path contains "(x86)".  Feeding that through `for /f ... in ( ... )`
REM re-opens the same parsing hazard the note above describes: the ')' inside the
REM expanded path terminates the block early and the command degenerates into a bare
REM "vswhere.exe", which cmd then reports as "not recognized".  Observed on this
REM machine's PowerShell-launched cmd.  So the result goes through a file instead of
REM through a command substitution.
set "VSPROBE=%TEMP%\qd_vswhere.txt"
if exist "%VSPROBE%" del "%VSPROBE%" >nul 2>&1
"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath >"%VSPROBE%" 2>nul
if not exist "%VSPROBE%" goto :no_vstools
for /f "usebackq delims=" %%i in ("%VSPROBE%") do set "VSPATH=%%i"
del "%VSPROBE%" >nul 2>&1
if not defined VSPATH goto :no_vstools
if not exist "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" goto :no_vstools
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :no_vcvars

:have_msvc
if not exist "%OUT%" mkdir "%OUT%"

echo Building winmm.dll ...
REM /std:c++17 is required: qdlog.h uses inline variables for the process-wide log
REM handle (a function-local static would need a thread-safe init guard, which is
REM exactly the machinery that must not run this early).
REM /MT matches a static CRT: the DLL is loaded by the game before its own CRT is
REM initialised, so it must not require a runtime it cannot see.  ws2_32 is the
REM transport; nothing else is needed (no CRT varargs, no STL in hot paths).
REM /Zi + /DEBUG emit a PDB: the module is a stripped release DLL, so without it a
REM crash report is only an RVA (`WINMM+0x3b357`) and a human has to guess.  With it,
REM dumpbin /DISASM:NOBYTES /LINENUMBERS, or a dump's stack, names the function AND
REM the line.  /OPT:REF,ICF /INCREMENTAL:NO are spelled out because /DEBUG would
REM otherwise leave the linker's debug defaults (no COMDAT folding beyond 8) in place:
REM the release code layout is the thing under test, so it must stay as it ships.
cl /nologo /LD /O2 /std:c++17 /EHsc /W3 /MT /D_CRT_SECURE_NO_WARNINGS /DWIN32_LEAN_AND_MEAN ^
   /D_WINSOCK_DEPRECATED_NO_WARNINGS /Zi ^
   "%SRC%\proxy.cpp" "%SRC%\fwd_symbols.c" ^
   "%MH%\src\buffer.c" "%MH%\src\hook.c" "%MH%\src\trampoline.c" "%MH%\src\hde\hde64.c" ^
   /I"%MH%\include" /I"%MH%\src" /I"%SRC%" ^
   /Fe"%OUT%\winmm.dll" /Fo"%OUT%\\" ^
   /link /DEF:"%HERE%\proxy.def" /MACHINE:X64 /DYNAMICBASE /NXCOMPAT ^
   /DEBUG /PDB:"%OUT%\winmm.pdb" /OPT:REF /OPT:ICF /INCREMENTAL:NO ^
   /MAP:"%OUT%\winmm.map" /IMPLIB:"%OUT%\winmm.lib" ws2_32.lib user32.lib
if errorlevel 1 goto :build_failed

echo.
echo Built %OUT%\winmm.dll
for %%f in ("%OUT%\winmm.dll") do echo   size: %%~zf bytes
echo.
echo Next: python tools\pe_export_table.py "%OUT%\winmm.dll"
echo       (expect "forwarders: 180  real code: 0" -- a proxy with real exports
echo        under a system name is how a loader failure becomes someone else's bug)
goto :eof

:no_targets
echo ERROR: src\targets.h missing. Run: python tools\gen_targets.py
exit /b 1
:no_exports
echo ERROR: src\exports.h missing. Run: python tools\gen_proxy_def.py --dll C:\Windows\System32\winmm.dll --library winmm --module C:\Windows\System32\winmm --out native\proxy.def --header native\src\exports.h --cstubs native\src\fwd_symbols.c
exit /b 1
:no_cstubs
echo ERROR: src\fwd_symbols.c missing. See the gen_proxy_def.py line above.
exit /b 1
:no_def
echo ERROR: native\proxy.def missing. See the gen_proxy_def.py line above.
exit /b 1
:no_minhook
echo ERROR: third_party\minhook is missing. It is vendored; restore it before building.
exit /b 1
:no_vswhere
echo ERROR: vswhere not found at "%VSWHERE%".
exit /b 1
:no_vstools
echo ERROR: MSVC x64 tools not found. Install "Desktop development with C++".
exit /b 1
:no_vcvars
echo ERROR: vcvars64.bat failed.
exit /b 1
:build_failed
echo BUILD FAILED
exit /b 1
