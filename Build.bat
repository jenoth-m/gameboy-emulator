@echo off
setlocal
pushd "%~dp0"

rem Find the C++ compiler installed with Visual Studio.
set "GB_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%GB_VSWHERE%" goto missing

set "GB_VS_PATH="
for /f "usebackq tokens=*" %%i in (`"%GB_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "GB_VS_PATH=%%i"
if not defined GB_VS_PATH goto missing

call "%GB_VS_PATH%\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
if errorlevel 1 goto failed

if not exist build mkdir build
if not exist build\roms mkdir build\roms

echo Building the emulator...
cl /nologo /std:c++17 /EHsc /W4 /permissive- /O2 /DUNICODE /D_UNICODE /DNOMINMAX /Isrc ^
    src\CPU.cpp src\Bus.cpp src\Cartridge.cpp src\PPU.cpp src\windows_main.cpp /Fobuild\ ^
    /Febuild\GameBoyEmulator.exe /link /SUBSYSTEM:WINDOWS user32.lib gdi32.lib comdlg32.lib
if errorlevel 1 goto failed

echo Building the command-line runner...
cl /nologo /std:c++17 /EHsc /W4 /O2 /Isrc src\headless.cpp build\CPU.obj build\Bus.obj ^
    build\Cartridge.obj build\PPU.obj /Fobuild\ /Febuild\gb_headless.exe
if errorlevel 1 goto failed

echo Building the small test programs...
cl /nologo /std:c++17 /EHsc /W4 /O2 /Isrc tests\tests.cpp build\CPU.obj build\Bus.obj ^
    build\Cartridge.obj build\PPU.obj /Fobuild\ /Febuild\gb_tests.exe
if errorlevel 1 goto failed

cl /nologo /std:c++17 /EHsc /W4 /O2 /Isrc tests\games_tests.cpp build\CPU.obj build\Bus.obj ^
    build\Cartridge.obj build\PPU.obj /Fobuild\ /Febuild\gb_games_tests.exe
if errorlevel 1 goto failed

copy /Y roms\*.gb build\roms\ >nul
if errorlevel 1 goto failed

echo Running the checks...
build\gb_tests.exe
if errorlevel 1 goto failed

build\gb_games_tests.exe roms
if errorlevel 1 goto failed

echo.
echo SUCCESS. Double-click Run.bat.
echo Press Enter to start. Use Left and Right to move.
popd
pause
exit /b 0

:missing
echo.
echo Visual Studio C++ tools were not found.
echo In Visual Studio Installer, choose Modify and add Desktop development with C++.
goto finish_error

:failed
echo.
echo BUILD OR TEST FAILED. Read the first error above.

:finish_error
popd
pause
exit /b 1
