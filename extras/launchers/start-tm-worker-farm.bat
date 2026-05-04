@echo off
REM TaskMessenger Worker Farm Launcher Script for Windows

setlocal enabledelayedexpansion

REM Configuration
set "DEFAULT_INSTALL_DIR=%LOCALAPPDATA%\TaskMessenger"

REM Get the directory of this script
set "SCRIPT_DIR=%~dp0"
set "SCRIPT_DIR=%SCRIPT_DIR:~0,-1%"

REM Determine worker-farm executable path
if exist "%SCRIPT_DIR%\..\bin\tm-worker-farm.exe" (
    set "WORKER_FARM_BIN=%SCRIPT_DIR%\..\bin\tm-worker-farm.exe"
) else if exist "%DEFAULT_INSTALL_DIR%\tm-worker\tm-worker-farm.exe" (
    set "WORKER_FARM_BIN=%DEFAULT_INSTALL_DIR%\tm-worker\tm-worker-farm.exe"
) else (
    where tm-worker-farm.exe >nul 2>&1
    if !errorlevel! equ 0 (
        set "WORKER_FARM_BIN=tm-worker-farm.exe"
    ) else (
        echo [ERROR] Could not find worker-farm executable >&2
        exit /b 1
    )
)

"%WORKER_FARM_BIN%" %*