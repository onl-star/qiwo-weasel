@echo off
setlocal

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build-qiwo.ps1" %*
exit /b %ERRORLEVEL%
