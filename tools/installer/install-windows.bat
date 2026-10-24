@echo off
rem Wind Waker HD setup in a console window (Windows): the fallback for the window of "Wind Waker HD.exe".
rem The same program runs the setup in this console instead (--console-setup): it runs
rem tools\installer\setup.py with the Python shipped in tools\python.
setlocal
pushd "%~dp0" || exit /b 1
cd ..
"%~dp0..\Wind Waker HD.exe" --console-setup %*
set "setup_result=%errorlevel%"
if not "%setup_result%"=="0" pause
popd
exit /b %setup_result%
