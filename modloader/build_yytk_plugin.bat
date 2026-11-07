@echo off
rem Shared MSVC build for small YYToolkit v5 plugins; Windows counterpart of build_yytk_plugin.sh.
rem usage: build_yytk_plugin.bat <plugin dir> <OutName> <source.cpp>...
setlocal
set "VCVARS="
if defined VSINSTALLDIR set "VCVARS=%VSINSTALLDIR%VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
  echo Visual Studio with the C++ x64 tools not found
  exit /b 1
)
call "%VCVARS%" >nul || exit /b 1
set "ROOT=%~dp0.."
set "YY=%ROOT%\external\YYToolkit\YYToolkit"
cd /d "%~1" || exit /b 1
set "NAME=%~2"
shift & shift
set "SRCS="
:args
if "%~1"=="" goto build
set "SRCS=%SRCS% %1"
shift
goto args
:build
if not exist build mkdir build
cl /nologo /std:c++latest /EHa /O2 /MD /W3 /LD /DNDEBUG /DYYTK_DEFINE_INTERNAL=1 /Fobuild\ /Fdbuild\ ^
   /I "%ROOT%\modloader\yyc_interop\include" /I "%YY%\include" /I "%YY%\source\YYTK\Shared" ^
   /I "%ROOT%\modloader\yytk_probe\stub" ^
   %SRCS% "%YY%\source\YYTK\Shared\YYTK_Shared_Types.cpp" ^
   /link /OUT:build\%NAME%.dll /IMPLIB:build\%NAME%.lib /PDB:build\%NAME%.pdb /DEBUG /INCREMENTAL:NO user32.lib || exit /b 1
echo Built build\%NAME%.dll
