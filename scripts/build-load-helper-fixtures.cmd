@echo off
setlocal
if not defined VSCMD_VER call "C:\BuildTools\VS2022\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b %errorlevel%
if not exist build\load-helper mkdir build\load-helper
cl /nologo /std:c++17 /O2 /W4 /EHsc /MT test\native\load-helper-target.cpp /Fobuild\load-helper\fixture-target.obj /Febuild\load-helper\fixture-target.exe /link /SUBSYSTEM:CONSOLE /MANIFEST:EMBED /MANIFESTUAC:"level='asInvoker' uiAccess='false'"
if errorlevel 1 exit /b %errorlevel%
cl /nologo /std:c++17 /O2 /W4 /EHsc /MT /LD test\native\load-helper-module.cpp /Fobuild\load-helper\fixture-module.obj /Febuild\load-helper\fixture-module.dll
exit /b %errorlevel%
