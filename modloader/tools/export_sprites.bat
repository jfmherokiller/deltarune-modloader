@echo off
rem export_sprites.bat <chapter 1-5> <ModName> <sprite> [sprite ...]
rem Copies the game's original sprite frames into mods\<ModName>\chapter<N>_windows\sprites\,
rem named and laid out the way the loader reads them. Wildcards work: spr_susie_walk_*
rem Run it from anywhere; it finds the game folder from its own location (mods\tools\).
setlocal EnableDelayedExpansion
if "%~3"=="" (
    echo usage: export_sprites.bat ^<chapter 1-5^> ^<ModName^> ^<sprite^> [sprite ...]
    echo   e.g. export_sprites.bat 2 MyMod spr_city_mice_bell
    echo        export_sprites.bat 1 MyMod "spr_krisd*"
    exit /b 1
)
set "TOOLS=%~dp0"
for %%I in ("%TOOLS%..\..") do set "GAME=%%~fI"
set "CH=%~1"
set "MOD=%~2"
set "DATA=%GAME%\chapter%CH%_windows\data.win"
if not exist "%DATA%" (
    echo chapter %CH% not found: "%DATA%"
    exit /b 1
)
set "CLI=%TOOLS%utmt\UndertaleModCli.exe"
if not exist "%CLI%" (
    echo UndertaleModCli.exe not found in "%TOOLS%utmt\" - see the README install step
    exit /b 1
)
shift & shift
set "LIST="
:args
if "%~1"=="" goto run
set "LIST=!LIST!%~1;"
shift
goto args
:run
set "DR_SPRITES=%LIST%"
set "DR_OUT_DIR=%GAME%\mods\%MOD%\chapter%CH%_windows\sprites"
"%CLI%" load "%DATA%" -s "%TOOLS%ExportSprites.csx" 2>&1 | findstr /b /c:"[DR]"
echo.
echo Edit the PNGs in "%DR_OUT_DIR%" (keep the file names), then start the game.
echo Check mods\modloader.gml.log for what was replaced.
