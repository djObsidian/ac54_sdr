@echo off
setlocal
set VCVARS="C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call %VCVARS% >nul 2>&1
cl /nologo /O2 /W3 /Fe:rtwoid.exe rtwoid.c iphlpapi.lib
if errorlevel 1 (
  echo BUILD FAILED
  exit /b 1
)
echo BUILD OK
del rtwoid.obj 2>nul
endlocal
