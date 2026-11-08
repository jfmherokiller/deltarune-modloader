@echo off
rem Builds build\LooseMods.dll (see ..\build_yytk_plugin.bat).
call "%~dp0..\build_yytk_plugin.bat" "%~dp0." LooseMods source\ModuleMain.cpp
