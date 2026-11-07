@echo off
rem Builds build\YYCRuntimeDump.dll (see ..\build_yytk_plugin.bat).
call "%~dp0..\build_yytk_plugin.bat" "%~dp0." YYCRuntimeDump source\ModuleMain.cpp
