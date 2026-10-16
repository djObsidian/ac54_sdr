@echo off
rem usage: mk.bat name   (builds name.c -> name.exe with libusb)
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" 10.0.20348.0 >nul 2>&1
rem vcvars sometimes fails to detect the SDK -> add it explicitly
set SDKI=C:\Program Files (x86)\Windows Kits\10\Include\10.0.20348.0
set SDKL=C:\Program Files (x86)\Windows Kits\10\Lib\10.0.20348.0
set INCLUDE=%INCLUDE%;%SDKI%\ucrt;%SDKI%\um;%SDKI%\shared
set LIB=%LIB%;%SDKL%\ucrt\x64;%SDKL%\um\x64
cl /nologo /O2 /W3 /MD /I libusb %1.c /Fe:%1.exe /link /LIBPATH:libusb libusb-1.0.lib
if errorlevel 1 ( echo BUILD FAILED & exit /b 1 )
del %1.obj 2>nul
echo BUILD OK
