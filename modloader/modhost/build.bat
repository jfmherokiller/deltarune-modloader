@echo off
rem Builds build\ModHost.dll and the example build\HelloPlugin.dll (see ..\build_yytk_plugin.bat).
rem The manifest tests (tests\resolve.cpp) are plain C++: build.sh test, or compile them with cl.
call "%~dp0..\build_yytk_plugin.bat" "%~dp0." ModHost /I "%~dp0include" source\ModuleMain.cpp source\Manifest.cpp
if errorlevel 1 exit /b 1
call "%~dp0..\build_yytk_plugin.bat" "%~dp0." HelloPlugin /I "%~dp0include" example\HelloPlugin.cpp
