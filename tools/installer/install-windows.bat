@echo off
rem Wind Waker HD setup in a console window (Windows): the fallback for the window of "Wind Waker HD.exe".
rem The same program runs the setup in this console instead (--console-setup): it runs
rem tools\installer\setup.py with the Python shipped in tools\python.
setlocal
cd /d "%~dp0.."
"%~dp0..\Wind Waker HD.exe" --console-setup %*
if errorlevel 1 pause
