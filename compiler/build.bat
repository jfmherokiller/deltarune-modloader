@echo off
rem Builds compiler\build\win-x64\LooseModCompiler.exe (self-contained, trimmed, ReadyToRun).
rem Install: copy compiler\build\win-x64\* to <game>\mods\tools\compiler\. Needs the .NET 10 SDK.
setlocal
cd /d "%~dp0"
if not exist ..\external\UndertaleModTool\UndertaleModLib\UndertaleModLib.csproj (
  echo submodule missing: git submodule update --init --recursive
  exit /b 1
)
if exist build\win-x64 rmdir /s /q build\win-x64
dotnet publish LooseModCompiler\LooseModCompiler.csproj -c Release -r win-x64 -nologo -v q -o build\win-x64 || exit /b 1
git -C ..\external\UndertaleModTool rev-parse HEAD > build\win-x64\UNDERTALEMODTOOL_COMMIT.txt
echo Built compiler\build\win-x64
