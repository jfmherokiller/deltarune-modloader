@echo off
rem Builds modloader\yytk_plugin\build\DeltaruneYYTK.dll (YYToolkit v5 plugin, MSVC 2022).
rem Headers in include\ come from YYToolkit's experimental branch (see include\YYTK_SOURCE_COMMIT.txt),
rem which matches the YYToolkit.dll v5.0.0c release binary.
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
cl /nologo /std:c++latest /EHsc /O2 /MD /W3 /LD /DNDEBUG /DYYTK_DEFINE_INTERNAL=0 /Fobuild\ /Fdbuild\ /I include ^
   source\ModuleMain.cpp include\YYToolkit\YYTK_Shared_Types.cpp ^
   /link /OUT:build\DeltaruneYYTK.dll /IMPLIB:build\DeltaruneYYTK.lib /PDB:build\DeltaruneYYTK.pdb /DEBUG /INCREMENTAL:NO user32.lib || exit /b 1
echo Built build\DeltaruneYYTK.dll
