@echo off
REM ==============================================================
REM  Environment X6 Sensor - GUI launcher
REM  1) activate the uv virtualenv  2) run python main.py
REM ==============================================================
setlocal
cd /d "%~dp0"

echo Project dir : %CD%

if not exist ".venv\Scripts\activate.bat" (
    echo [!] .venv not found - initializing environment with uv ...
    uv sync
    if errorlevel 1 (
        echo [X] "uv sync" failed.
        echo [X] Install uv first: winget install astral-sh.uv
        echo [X] Docs: https://docs.astral.sh/uv/
        echo.
        pause
        exit /b 1
    )
)

echo [1/2] Activating virtualenv: .venv\Scripts\activate.bat
call ".venv\Scripts\activate.bat"
if errorlevel 1 (
    echo [X] Failed to activate virtualenv.
    echo.
    pause
    exit /b 1
)

echo [2/2] Starting: python main.py
python main.py
set "RC=%ERRORLEVEL%"

if "%RC%"=="0" (
    endlocal
    exit /b 0
)

rem --- error path: keep the window open so the message can be read ---
echo.
echo [X] Program exited with code %RC% - see message above.
echo.
echo Press any key to close this window...
pause >nul
endlocal
exit /b %RC%
