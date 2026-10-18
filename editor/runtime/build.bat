@echo off
setlocal
set "VCVARS="
if defined VSINSTALLDIR set "VCVARS=%VSINSTALLDIR%VC\Auxiliary\Build\vcvars64.bat"
if not defined VCVARS for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VCVARS=%%i\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (echo Visual Studio x64 C++ tools not found & exit /b 1)
call "%VCVARS%" >nul || exit /b 1
cd /d "%~dp0"
if not exist build mkdir build
if /i "%~1"=="test" goto tests
rem Stage CURRENT fork headers only within editor/runtime; do not mutate the sample plugin.
if not exist "..\..\external\YYToolkit\YYToolkit\source\YYTK\Shared\YYTK_Shared.hpp" (echo YYToolkit fork missing & exit /b 1)
xcopy /y /i /q "..\..\external\YYToolkit\YYToolkit\source\YYTK\Shared\YYTK_Shared*" "build\include\YYToolkit\" >nul || exit /b 1
xcopy /y /i /q "..\..\external\YYToolkit\YYToolkit\include\Aurie\shared.hpp" "build\include\Aurie\" >nul || exit /b 1
xcopy /y /i /q "..\..\external\YYToolkit\YYToolkit\include\FunctionWrapper\FunctionWrapper.hpp" "build\include\FunctionWrapper\" >nul || exit /b 1
git -C ..\..\external\YYToolkit rev-parse HEAD > build\YYTK_SOURCE_COMMIT.txt
cl /nologo /std:c++latest /EHsc /O2 /MD /W4 /LD /DNDEBUG /DNOMINMAX /DYYTK_DEFINE_INTERNAL=0 /Fobuild\ /Fdbuild\ /I build\include ^
 source\ModuleMain.cpp source\RuntimeBridge.cpp source\InspectorBridge.cpp source\InspectorProtocol.cpp source\InspectorValue.cpp source\InspectorSampler.cpp build\include\YYToolkit\YYTK_Shared_Types.cpp ^
 /link /OUT:build\RunnerEditor.dll /IMPLIB:build\RunnerEditor.lib /PDB:build\RunnerEditor.pdb /DEBUG /INCREMENTAL:NO user32.lib advapi32.lib || exit /b 1
echo Built editor/runtime/build/RunnerEditor.dll - NOT INSTALLED
exit /b 0
:tests
cl /nologo /std:c++20 /EHsc /O2 /MD /W4 /DNOMINMAX /Fobuild\ /Fdbuild\ tests\native.cpp source\RuntimeBridge.cpp /link /OUT:build\native-tests.exe advapi32.lib || exit /b 1
build\native-tests.exe || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MD /W4 /DNOMINMAX /Fobuild\ /Fdbuild\ tests\inspector.cpp source\InspectorProtocol.cpp source\InspectorValue.cpp /link /OUT:build\inspector-tests.exe || exit /b 1
build\inspector-tests.exe || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MD /W4 /DNOMINMAX /Fobuild\ /Fdbuild\ tests\inspector-transport.cpp source\InspectorBridge.cpp source\InspectorProtocol.cpp source\RuntimeBridge.cpp /link /OUT:build\inspector-transport-tests.exe advapi32.lib || exit /b 1
build\inspector-transport-tests.exe || exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /MD /W4 /DNOMINMAX /I tests\fake /Fobuild\ /Fdbuild\ tests\inspector-sampler.cpp source\InspectorSampler.cpp source\InspectorValue.cpp source\InspectorProtocol.cpp source\RuntimeBridge.cpp /link /OUT:build\inspector-sampler-tests.exe advapi32.lib || exit /b 1
build\inspector-sampler-tests.exe
exit /b %errorlevel%
