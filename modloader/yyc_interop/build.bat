@echo off
rem Builds build\YYCHookDemo.dll (see ..\build_yytk_plugin.bat).
call "%~dp0..\build_yytk_plugin.bat" "%~dp0." YYCHookDemo example\HookDemo.cpp
