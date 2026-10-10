# Settings overlay controls

Press F1 to open Settings. On a controller, press Home or hold Select / Minus.
Use the D-pad or left stick to navigate, A to select, B to go back or close,
and L / R to switch tabs.

Mouse movement, clicking, wheel scrolling, and dragging the scroll bar take over
from controller navigation, including when the controller is centered. A stick or
trigger already held when you use the mouse will not keep moving the menu. Release it and make a new controller input
to return to controller navigation. The right stick does not navigate the overlay.
Keyboard navigation remains available while using the mouse.

## Diagnostics

If asked to capture a menu problem, `WWHD_OVERLAY_TRACE=1` records controller
axes (raw and normalized, including triggers), held buttons, menu input events,
focus/navigation and scroll changes in `captures/wwhd.log` while Settings is open.
It enables the normal log file for that run (unless `WWHD_LOG_FILE=0` is set).
Unchanged input is omitted; very noisy input is rate limited with a notice, and
trace output stops after about 512 KiB. Restart to capture another session.

`WWHD_OVERLAY_MOUSE_HANDOFF=0` temporarily disables mouse ownership of controller
navigation, so the original scrolling behavior can be tested. Normally leave it
unset: mouse handoff stays enabled by default.

On Windows, put a file named `menu-diagnostics.bat` beside `wwhd.exe`, containing:

```bat
@echo off
setlocal
cd /d "%~dp0"
set "WWHD_OVERLAY_TRACE=1"
set "WWHD_OVERLAY_MOUSE_HANDOFF=0"
wwhd.exe
endlocal
```

Double-click it to launch. Use the stick once and release it, open Settings with
F1, click an option, then scroll down with the mouse wheel. Wait a few seconds,
quit, and attach `captures/wwhd.log` to the issue. Keep the session around two
minutes or less. The variables apply only to this launch; launching `wwhd.exe`
normally restores the defaults.
