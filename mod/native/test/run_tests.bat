@echo off
REM ---------------------------------------------------------------------------
REM Build and run every test_*.cpp in this directory.
REM
REM These tests exist because the parts of this mod that are hardest to get right
REM are the parts that cannot be debugged inside a running game: interpolation
REM timing, a spawn lifecycle, a set union, a lock, and a monotonic counter.  A
REM mistake in any of them looks like "the other player is invisible" or "the door
REM did not open", which is indistinguishable from a sync failure at the cost of a
REM game run.  Here they are a two-second unit test.
REM
REM Iterating over test_*.cpp rather than naming files means a new test is picked up
REM without editing this script -- and a test that is not built is the same as a
REM test that does not exist.
REM ---------------------------------------------------------------------------
setlocal enabledelayedexpansion

set HERE=%~dp0
set OUT=%HERE%build
set "PF86=%ProgramFiles(x86)%"
set "VSWHERE=%PF86%\Microsoft Visual Studio\Installer\vswhere.exe"

if defined VSCMD_ARG_TGT_ARCH goto :have_msvc
if not exist "%VSWHERE%" goto :no_vswhere
set "VSPATH="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSPATH=%%i"
if not defined VSPATH goto :no_vstools
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 goto :no_vcvars

:have_msvc
if not exist "%OUT%" mkdir "%OUT%"

set FAILED=0
set RAN=0

for %%f in ("%HERE%test_*.cpp") do call :one "%%~ff" "%%~nf"
goto :summary

:one
set SRC=%~1
set NAME=%~2
set RAN=1
echo.
echo === %NAME% ===
REM /W4 and treat nothing as suppressed: a warning here is a numeric mistake in
REM disguise, which is the dominant error mode in this codebase's history.
REM _CRT_SECURE_NO_WARNINGS is for fopen in the vector test, which reads a file that
REM is checked in -- the "safe" variant buys nothing here.
cl /nologo /std:c++17 /O2 /EHsc /W4 /MT /D_CRT_SECURE_NO_WARNINGS /I"%HERE%..\src" "%SRC%" ^
   /Fe:"%OUT%\%NAME%.exe" /Fo:"%OUT%\%NAME%.obj" >"%OUT%\%NAME%.build.log" 2>&1
if errorlevel 1 goto :one_build_failed
type "%OUT%\%NAME%.build.log"
"%OUT%\%NAME%.exe"
if errorlevel 1 set /a FAILED+=1
goto :eof

:one_build_failed
echo BUILD FAILED
type "%OUT%\%NAME%.build.log"
set /a FAILED+=1
goto :eof

:summary
echo.
if %RAN%==0 goto :no_tests
if %FAILED%==0 goto :all_pass
echo ============================================
echo  FAILURES: %FAILED% test executable(s) failed
echo ============================================
exit /b 1

:all_pass
echo ============================================
echo  all test executables passed
echo ============================================
exit /b 0

:no_tests
echo ERROR: no test_*.cpp found next to this script
exit /b 1
:no_vswhere
echo ERROR: vswhere not found at "%VSWHERE%"
exit /b 1
:no_vstools
echo ERROR: MSVC x64 tools not found
exit /b 1
:no_vcvars
echo ERROR: vcvars64.bat failed
exit /b 1
