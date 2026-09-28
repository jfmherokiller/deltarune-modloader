@echo off
rem Builds modloader\build\version.dll with MSVC (VS 2022 Build Tools / Community).
setlocal
set "VCVARS="
if defined VSINSTALLDIR set "VCVARS=%VSINSTALLDIR%VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo Visual Studio with the C++ x64 tools not found
  exit /b 1
)
call "%VCVARS%" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
set MH=third_party\minhook
cl /nologo /O2 /MT /W3 /GS /LD /D_CRT_SECURE_NO_WARNINGS /Fobuild\ /Fdbuild\ /I %MH%\include ^
   src\modloader.c src\exports.c %MH%\src\hook.c %MH%\src\buffer.c %MH%\src\trampoline.c %MH%\src\hde\hde64.c ^
   /link /DEF:src\version.def /OUT:build\version.dll /PDB:build\version.pdb /IMPLIB:build\version.lib /DEBUG /INCREMENTAL:NO kernel32.lib || exit /b 1
echo Built build\version.dll
