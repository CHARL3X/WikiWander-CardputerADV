@echo off
REM Build and run the live walker test. Hits real Wikipedia, so
REM network is required. ~2-3 seconds typical run time.
REM
REM Usage from project root: test\run_walker.bat

setlocal

set "PROJECT_DIR=%~dp0.."
cd /d "%PROJECT_DIR%"

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" goto NoVcVars

call "%VCVARS%" > nul
if errorlevel 1 goto VcVarsFailed

if not exist .pio\build\native mkdir .pio\build\native

REM walker pulls in wiki_client + parser + curl transport.
cl /nologo /EHsc /std:c++17 /W3 /I lib\wiki /I test /I vendor /D WIKIWANDER_PC_BUILD /Fo.pio\build\native\\ /Fe.pio\build\native\walker.exe lib\wiki\wiki_parser.cpp lib\wiki\wiki_client.cpp lib\wiki\extract_html.cpp test\transport_curl.cpp src\test_walker.cpp
if errorlevel 1 goto BuildFailed

echo.
echo --- running walker ---
.pio\build\native\walker.exe
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
