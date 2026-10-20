@echo off
cd /d "%~dp0"
title Website Launcher

echo Checking Python environment...

if not exist ".venv\Scripts\python.exe" (
    python -m venv .venv
    if errorlevel 1 goto error

    .venv\Scripts\python.exe -m pip install -r requirements.txt
    if errorlevel 1 goto error
)

echo Starting website server...
start "Website Server" /min cmd /c ""%~dp0.venv\Scripts\python.exe" "%~dp0server.py""

timeout /t 3 /nobreak >nul

start "" "http://127.0.0.1:8000"

exit /b

:error
echo.
echo ERROR: Something went wrong. Read the messages above.
pause
