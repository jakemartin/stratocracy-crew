@echo off
REM Run the crew with MSVC's cl.exe on PATH (Assignment #10 cost runs).
REM
REM crew/tools.py's find_compiler() looks for g++, clang++, c++, then cl. Only cl is
REM installed on this machine, and cl is ONLY on PATH inside a VS developer environment
REM -- so a bare `python run.py` reports "no C++ compiler on PATH" and the gate cannot
REM run. This wrapper sources vcvars64 first, then forwards every argument to run.py.
REM
REM   run_with_msvc.bat --offline
REM   run_with_msvc.bat --online
REM
REM vcvars64 prints a banner to stdout; it is silenced so it cannot be mistaken for
REM pipeline output in a captured log. Its exit code is still checked.

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
    echo [run_with_msvc] vcvars64.bat not found at "%VCVARS%"
    exit /b 2
)

call "%VCVARS%" >nul
if errorlevel 1 (
    echo [run_with_msvc] vcvars64.bat failed
    exit /b 2
)

python run.py %*
exit /b %errorlevel%
