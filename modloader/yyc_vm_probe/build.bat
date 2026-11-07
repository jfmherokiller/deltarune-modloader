@echo off
rem Builds build\YYCVmProbe.dll (see ..\build_yytk_plugin.bat).
call "%~dp0..\build_yytk_plugin.bat" "%~dp0." YYCVmProbe source\ModuleMain.cpp
