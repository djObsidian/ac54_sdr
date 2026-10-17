@echo off
rem usage: mk.bat name   (builds name.c -> name.exe with libusb)
setlocal
for /f "usebackq tokens=*" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "RTW_VS=%%i"
if not defined RTW_VS ( echo MSVC installation not found & exit /b 1 )
call "%RTW_VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
rem SDK discovery can fail in restricted shells; use the newest installed SDK.
for /f "delims=" %%v in ('dir /b /ad /o-n "%ProgramFiles(x86)%\Windows Kits\10\Include\10.*"') do if not defined RTW_SDK set "RTW_SDK=%%v"
set "SDKI=%ProgramFiles(x86)%\Windows Kits\10\Include\%RTW_SDK%"
set "SDKL=%ProgramFiles(x86)%\Windows Kits\10\Lib\%RTW_SDK%"
set "INCLUDE=%INCLUDE%;%SDKI%\ucrt;%SDKI%\um;%SDKI%\shared"
set "LIB=%LIB%;%SDKL%\ucrt\x64;%SDKL%\um\x64"
cl /nologo /O2 /W3 /MD /I libusb %1.c /Fe:%1.exe /link /LIBPATH:libusb libusb-1.0.lib
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
del %1.obj 2>nul
echo BUILD OK
