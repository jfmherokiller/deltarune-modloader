@echo off
rem Builds modloader\yytk_probe\build\YYTKProbe.dll against the YYToolkit submodule headers.
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
set YY=..\..\external\YYToolkit\YYToolkit
if not exist build mkdir build
cl /nologo /std:c++latest /EHa /O2 /MD /W3 /LD /DNDEBUG /DYYTK_DEFINE_INTERNAL=1 /Fobuild\ /Fdbuild\ ^
   /I %YY%\include /I %YY%\source\YYTK\Shared /I stub ^
   source\ModuleMain.cpp %YY%\source\YYTK\Shared\YYTK_Shared_Types.cpp ^
   /link /OUT:build\YYTKProbe.dll /IMPLIB:build\YYTKProbe.lib /PDB:build\YYTKProbe.pdb /DEBUG /INCREMENTAL:NO user32.lib || exit /b 1
echo Built build\YYTKProbe.dll
