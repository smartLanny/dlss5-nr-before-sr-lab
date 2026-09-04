@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0collect-crash-feedback.ps1" -SourceDirectory "%~dp0."
