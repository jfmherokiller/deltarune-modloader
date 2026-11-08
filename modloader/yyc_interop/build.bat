@echo off
rem Builds build\YYCHookDemo.dll and build\ScriptHookDemo.dll (see ..\build_yytk_plugin.bat).
call "%~dp0..\build_yytk_plugin.bat" "%~dp0." YYCHookDemo example\HookDemo.cpp
if errorlevel 1 exit /b 1
call "%~dp0..\build_yytk_plugin.bat" "%~dp0." ScriptHookDemo example\ScriptHookDemo.cpp
