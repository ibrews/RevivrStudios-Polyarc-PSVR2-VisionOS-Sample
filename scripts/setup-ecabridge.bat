@echo off
REM Per-machine ECABridge setup (Windows). See scripts/setup-ecabridge.sh for the full rationale.
REM Plugins\ECABridge is gitignored: ECABridge is an Editor-only dev/MCP bridge, not project content.
REM A junction is used rather than a symlink so no developer-mode/elevation is required.
setlocal
set "PROJ=%~dp0.."
if "%ECABRIDGE_SRC%"=="" set "ECABRIDGE_SRC=H:\GH\ECABridge"
if not exist "%ECABRIDGE_SRC%" (
  echo ECABridge checkout not found at %ECABRIDGE_SRC%. Clone it, or set ECABRIDGE_SRC. 1>&2
  exit /b 3
)
if exist "%PROJ%\Plugins\ECABridge" (
  echo Plugins\ECABridge already exists -- leaving it alone.
  exit /b 0
)
mklink /J "%PROJ%\Plugins\ECABridge" "%ECABRIDGE_SRC%"
echo.
echo Then enable it locally ^(do NOT commit this^): set ECABridge Enabled=true in Lumenwork.uproject
