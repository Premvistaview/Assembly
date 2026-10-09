@echo off
powershell -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0PlaceAssembly.ps1" %*
exit /b %ERRORLEVEL%
