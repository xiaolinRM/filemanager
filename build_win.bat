@echo off
rem Build the File Manager extension for Windows (x86).
rem
rem Requirements:
rem   - Visual Studio 2015 or newer (AMBuild locates it automatically)
rem   - Python 3.6+ with AMBuild installed:  pip install <path-to-ambuild>
rem   - A SourceMod source tree (default: ..\sourcemod)
rem
rem Usage:
rem   build_win.bat [--sm-path=...] [--enable-debug]

setlocal
set "ROOT=%~dp0"
set "SM_PATH=%ROOT%..\sourcemod"
set "PYTHONUTF8=1"

if not exist "%ROOT%build" mkdir "%ROOT%build"
cd /d "%ROOT%build"

python "%ROOT%configure.py" --sm-path="%SM_PATH%" --enable-optimize %*
if errorlevel 1 goto :failed

python -c "from ambuild2.run import cli_run; cli_run()"
if errorlevel 1 goto :failed

echo.
echo Build succeeded. Output is in "%ROOT%build\package".
exit /b 0

:failed
echo.
echo Build failed.
exit /b 1
