@echo off
rem This file is part of the dosbox-automation Project.
rem License: GPL-2.0-or-later. Contact: dosbox-automation-project@trinity2k.net
rem
rem Starts dosbox-automation with the web API on and a fresh token, then
rem prints the Cheat Workbench URL, a link carrying the token for a browser
rem on another machine, and the token itself.
rem
rem Usage: cheat-workbench.cmd [nowait]
rem   DOSBOX_BIN   emulator to start (default: dosbox.exe next to this script)
rem   DOSBOX_PORT  API port (default 8386)
setlocal
cd /d "%~dp0"

if "%DOSBOX_PORT%"=="" set DOSBOX_PORT=8386
if "%DOSBOX_BIN%"=="" set DOSBOX_BIN=%~dp0dosbox.exe
if not exist "%DOSBOX_BIN%" (
  echo error: %DOSBOX_BIN% not found; set DOSBOX_BIN to the emulator executable
  if not "%1"=="nowait" pause
  exit /b 1
)

rem Two GUIDs give the 64 hex characters the engine requires; no pipes or
rem braces, which a for /f command line would need escaping for.
for /f "usebackq delims=" %%t in (`powershell -NoProfile -Command "([guid]::NewGuid().ToString('N') + [guid]::NewGuid().ToString('N'))"`) do set DOSBOX_API_TOKEN=%%t
if "%DOSBOX_API_TOKEN%"=="" (
  echo error: token generation failed
  if not "%1"=="nowait" pause
  exit /b 1
)

set CONF_DIR=%TEMP%\dosbox-automation-workbench
if not exist "%CONF_DIR%" mkdir "%CONF_DIR%"
set CONF=%CONF_DIR%\workbench.conf
> "%CONF%" echo [webserver]
>> "%CONF%" echo webserver_enabled = on
>> "%CONF%" echo webserver_port = %DOSBOX_PORT%

start "" "%DOSBOX_BIN%" -conf "%CONF%"

echo.
echo dosbox-automation started.
echo.
echo   Cheat Workbench:  http://127.0.0.1:%DOSBOX_PORT%/tools/cheat-workbench.html
echo   Link with token:  http://127.0.0.1:%DOSBOX_PORT%/tools/cheat-workbench.html#token=%DOSBOX_API_TOKEN%
echo   API token:        %DOSBOX_API_TOKEN%
echo.
echo A browser on this machine connects on its own; type WORKBENCH at the DOS
echo prompt or press Ctrl+Alt+W to open it. The link is for a browser elsewhere.
echo The emulator keeps running when this window closes; stop it from the
echo Workbench setup or by closing its window.
echo.
if not "%1"=="nowait" pause
endlocal
