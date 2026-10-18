@echo off
setlocal
cd /d "%~dp0"
if not exist node_modules\electron (
  echo Run npm ci in this folder before starting Runner Studio.
  pause
  exit /b 1
)
call npm start -- %*
