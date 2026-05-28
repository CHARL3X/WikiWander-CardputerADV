@echo off
REM Build and run Wikiwander parser tests on the host using MSVC.
REM
REM Bypasses PlatformIO's native env (which expects gcc/g++ in PATH).
REM Uses the vendored ArduinoJson single-header at vendor/ArduinoJson.h.
REM
REM Usage from project root: test\run_native.bat

setlocal

set "PROJECT_DIR=%~dp0.."
cd /d "%PROJECT_DIR%"

REM Parens in the Program Files (x86) path break parens-style IF blocks
REM in CMD, so we keep everything as flat statements with single-line IFs.
set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto NoVcVars

call "%VCVARS%" > nul
if errorlevel 1 goto VcVarsFailed

if not exist .pio\build\native mkdir .pio\build\native

cl /nologo /EHsc /std:c++17 /W3 /I lib\wiki /I vendor /D WIKIWANDER_PC_BUILD /Fo.pio\build\native\\ /Fe.pio\build\native\test.exe lib\wiki\wiki_parser.cpp lib\wiki\extract_html.cpp src\test_main.cpp
if errorlevel 1 goto BuildFailed

echo.
echo --- running tests ---
.pio\build\native\test.exe
exit /b %ERRORLEVEL%

:NoVcVars
echo ERROR: vcvars64.bat not found at: %VCVARS%
exit /b 1

:VcVarsFailed
echo ERROR: vcvars64 setup failed
exit /b 1

:BuildFailed
echo.
echo BUILD FAILED
exit /b 1
