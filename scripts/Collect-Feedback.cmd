@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0collect-feedback.ps1" -SourceDirectory "%~dp0."
