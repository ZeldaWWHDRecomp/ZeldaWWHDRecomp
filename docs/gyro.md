# Gyro aiming

On the Wii U, Wind Waker HD lets you aim in first person by moving the GamePad: the bow, hookshot,
boomerang, telescope, Picto Box, grappling hook and the plain first-person look (R3) all use it.
The port has no GamePad, so it turns a **virtual GamePad** with one of these sources:

| Source | What it uses |
|---|---|
| Off (default) | the GamePad lies still: no gyro aiming, as before |
| Device gyro (phone/tablet) | the Android device’s own gyroscope and accelerometer; rotates with the displayed screen, works with touch or a controller |
| Controller gyro | the gyro and accelerometer of a DualSense, DualShock 4, Switch Pro, Joy-Con, Steam Deck / Steam Controller and other controllers SDL3 reads (the macOS app uses GameController.framework: DualSense, DualShock 4, Switch Pro, Joy-Con) |
| Cemuhook (DSU) | a Cemuhook motion server over UDP: DS4Windows, BetterJoy, SteamDeckGyroDSU, phone apps. Default 127.0.0.1, port 26760, slot 1 |
| Mouse aiming | mouse movement turns the GamePad while the game aims, also with the game's Options → Gyro off; for Steam Input's "gyro to mouse" output and a mouse without the mouse camera. With **Mouse camera** on (Controls → Camera) this source waits: the mouse camera aims in first person directly (issue #558, docs/controller-input.md) |

It works with both controller choices (Input → **Wii U GamePad** or **Wii U Pro Controller**, issue #71).
In Pro Controller mode the game's own **Options → Gyroscope** switch stays in the options menu and can be
changed as usual (checked in a game run); its help text still says it "will have no effect when using the
Wii U Pro Controller", which is the Wii U's behaviour: in the port it does apply while a gyro source is on.

Settings overlay (F1) → **Controls** → **Controller** → **Gyro…**:

- **Source** (above). `WWHD_GYRO=off|controller|cemuhook|mouse|device` overrides it at start.
- **Turn left/right by** (controller, device and Cemuhook sources), the usual gyro aiming conventions (as in
  JoyShockMapper and Steam Input):
  - **Player space** (default): turning the controller left or right about the real vertical,
    however you hold it (flat, tilted towards you, upright). Rolling it does nothing.
  - **Yaw**: turning it about its own vertical axis, as if it lay flat.
  - **Roll**: tilting it to the side like a steering wheel.

  Tilting the controller's top up or down always looks up or down.
- **Sensitivity left/right** and **up/down** (0.05x to 5x, default 0.5x; 1.0x with the device gyro),
  each with **Invert**. 1.0x turns the view as far as a real Wii U GamePad turned by the same angle
  would; the game turns its first-person view about 1.9 times the GamePad's left/right turn and 1.5
  times its up/down tilt (measured in R3 look), so 0.5x lets the view follow a controller about one
  to one. With the device gyro the screen you hold is the GamePad, so 1.0x matches it one to one.
  Choosing another source moves a sensitivity you have not changed to that source's default, and
  **Default sensitivity** sets the current source's default. A value you choose, 1.0x included, is
  kept. Settings saved by v0.2.5 to v0.2.7 with the old default 1.0x start at the new default.
- **Mouse: degrees per point**: how far one point of mouse movement turns the controller (then the
  sensitivity applies).
- **Cemuhook**: server, port and controller slot (1 to 4).
- **Recalibrate**: a controller input and/or a key, and a **Recalibrate now** button. If the view
  drifts while the controller rests, recalibrate and put it down for a second: the gyro's offset is
  learnt anew. (The view itself never needs recentering: the game only follows the motion. The
  binding is saved under its old name, `gyro.recenterPad` / `gyro.recenterKey`.)

Everything is saved with the other settings (`gyro.*` in `settings.ini`, or `display.plist` on
the macOS app). The game's own **Options → Gyro** switch (on by default in the game) still decides
whether the game uses the motion of a controller, Cemuhook or the device gyro. **Mouse aiming** works
with it off too (issue #525): the mouse is a PC aim device of its own, so the port reads the switch as on
while the mouse aims, without changing the saved option. The game ignores the motion while the right stick is pushed
(outside a 0.1 dead zone; a drifting stick does that too, see Troubleshooting).

Why off by default: the gyro moves the first-person camera with every hand movement, which surprises
players who never asked for it, and a controller lying on a desk works fine either way. The Gyro
window says when a connected controller has a gyro.

## How the game reads the GamePad

From the decompilation (`wwhd_src`):

- The game imports `VPADRead` and no VPAD gyro setup function, so it runs with the library defaults.
- `ControllerMgr::calc` (02617AF4) copies the **direction matrix** of the newest `VPADStatus` sample
  (+0x6C, +0x78, +0x84: the GamePad's X, Y and Z axes) every frame (026173B0). The gyro rate (+0x38),
  angle (+0x44) and accelerometer (+0x1C) are not read.
- Its only reader is `dCamera_c::CalcSubjectAngle` (02506964), the first-person ("subject") camera:
  `R = Cᵀ·M` with `C` the matrix of the previous frame (`calibrate`, 026185BC, runs every frame and
  when first person starts), yaw input `(R[2][0] − R[1][0])·30` (rotation about the GamePad's Y or Z,
  so flat and upright holding both work, and rolling a flat GamePad turns too), pitch input
  `R[2][1]·30`. Both are used like a right-stick value. It runs only when the options byte `+5` (the
  in-game Gyro switch, default on) is set, the controller mode is not 0, and the right stick is within
  its 0.1 dead zone.
- The controller mode (ControllerMgr +0x1D0) is 0 when the game plays with a **Pro Controller**,
  1 or 2 with the GamePad and 3 with both. `02618604`, which hands the camera the calibrated
  orientation, returns the identity (no motion) in mode 0: a real Pro Controller has no gyro and the
  GamePad lies on the table. The matrix is still copied every frame in that mode.
- Every item aim goes through that camera; no item code reads the gyro itself.

So what matters is how the direction matrix turns from one frame to the next, in the GamePad's own
frame. Measured in R3 look (`WWHD_TEST_GYRO`, Outset): a GamePad turn of 60° right turned the view
113° right, a tilt of 20° up 29° up.

## The mouse camera in first person (issue #558)

Measured with the Mouse aiming source (`WWHD_TEST_MOUSE`, R3 look on Windfall, style SS01) the game's
gyro input is lag-free and linear for slow movement, but not a PC shooter's mouse look:

- the input is `30·sin` of the GamePad's turn per step: a flick of 20° of GamePad in one step turns the
  view 2 % less than a slow turn, 50° 12 % less (the left/right range then clamps at 90° per step);
  turning left/right and up/down in the same step couples (a 10°/10° step: 8 % more left/right);
- left/right and up/down differ: the view turns `range × speed × 30` degrees per radian of GamePad, 108
  left/right and 84 up/down in R3 look, the bow, hookshot, boomerang and grappling hook (styles SS01, SX01;
  72 up/down in SX02, whose pitch range is 60°);
- the drawn view eases towards the angles (camera parameter 20): of a step's turn 45 % of the up/down and
  90 % of the left/right show in that step, the rest over the next five steps (stick and gyro alike). In the
  Picto Box and the telescope (style SN15) Link turns with the drawn view (procScope), so its ease (0.75) also
  scales left/right down to 0.75 there.

So with the mouse camera on, the mouse does not go through the gyro: `@02506CEC` (CalcSubjectAngle's GamePad
branch, the input already formed, before its swim inversion) adds the mouse's turn divided by the live
style's `range × speed` (`mods/camera_mouse.h`, `subject_step`), so one point turns the view by the mouse
sensitivity in every subject style; the game's clamps, pitch range, crawl rule and the telescope / Picto Box
zoom slow-down still apply. The game keeps its angles in s16 units (65536 a turn) and truncates each step's
turn (`cM_deg2s`), so the mouse's turn is landed on the nearest whole unit and the fraction carried to the
next step (without it, slow movement lost up to one unit a step, 1 % at 2 points a step); a flick faster than
the game's 90° a step carries the rest to the next steps. `@02508380` and `@025083C4` set the view's ease factor
to 1 while the mouse looks around (the radius and field of view keep theirs), which also gives the Picto Box
and the telescope their full left/right turn. At once, one more detail shows: when Link turns himself
(`setBodyAngleToCamera`, R3 look and the item aims), the camera's yaw target is Link's new facing plus the
turn again (0.45 × 2 = the 90 % above); `@025083C4` then aims the view at Link's facing, without that
overshoot. The game takes the GamePad branch only with the right stick at rest; it also runs the function
twice a step (Link, then the camera), so the movement is used once. Before, the mouse camera drove a synthetic
right stick in first person: rate control, saturated at 12.5 points per step (3.6° per step at most).

Measured after the change (mouse sensitivity 0.15° per point): 0.150° per point left/right and up/down in R3
look, the bow, hookshot, boomerang, grappling hook, telescope and Picto Box; a 150-point flick turns 22.5° in
the same step with no tail; 400 points 60°, 1000 points 90° and then 60° (the game's limit per step), a
diagonal 200/200 30°/30°. With the mouse camera off (Mouse aiming source) and with a controller's right stick
the camera angles are the same as before, step for step.

## Phone sensors

On Android choose **Device gyro (phone/tablet)** in **Settings > Controls > Controller > Gyro**.
It is off by default and requires both a gyro and an accelerometer. Rest the phone
for a second, enable the game’s own **Options > Gyro**, then aim in first person with
the right stick at rest. Player space, sensitivity, inversion and recalibration
work as with a controller. The default sensitivity here is 1.0x: the view turns as far
as the phone, like the real GamePad. Aiming follows the displayed screen in either landscape
direction. Sensors close in the background and while settings block gameplay, and
recalibrate on return. The status line reports unavailable sensors.
`WWHD_GYRO=device` selects this source; `WWHD_GYRO=off` disables it.

## How the port does it

`runtime/src/motion/`:

- `fusion.cpp`: the axis mapping of the SDL and Cemuhook sources into one frame (the signs follow the
  GamePad conventions Cemu recorded from real hardware; Cemu is MPL-2.0, as this port), gyro bias
  estimation while the controller rests (no slow drift), a noise floor, the gravity direction (the
  accelerometer, carried along by the gyro), and the **aim**: how far the player turned left/right
  and up/down under the chosen axis mode (player space: the turn about gravity with JoyShockMapper's
  "relax factor", so a tilted hold still turns easily).
  The aim, after sensitivity and invert, turns one **virtual GamePad** about its own axes (yaw about
  its Y axis, pitch about its X axis). The game reads exactly that turn whatever pose the virtual
  GamePad is in, so the view follows the same way however the controller is held, nothing jumps when
  a controller connects or disconnects or the source changes, and no pose can make a turn read as
  something else. (v0.2.5 to v0.2.7 mirrored the controller's whole pose instead: the game then also
  turned on rolling, and slowly found the controller's tilt; see issue #45.)
- `dsu.cpp`: a small Cemuhook client: one background thread, 100 ms receive timeout, a data request
  per second (servers drop quiet clients), CRC-checked packets, never blocks the game.
- `motion.cpp`: the sources, settings, the recalibrate binding and what `VPADRead` gets. With several
  controllers, the one that moves drives the GamePad (another takes over when it turns while the
  active one rests). Sensor timestamps that do not advance (a driver without them, a clock that
  stalls) fall back to the arrival times, so the motion never stops because of them. The mouse source
  only takes movement while the game aims (first-person camera or an item aim, from
  `mods/camera.cpp`); the pointer is captured then, and the mouse camera mod leaves the mouse alone.
- `game_hooks.cpp` (`tools/recomp/hooks_gyro.txt`): in Pro Controller mode, while a source is on,
  `02618604` skips its mode-0 check, so the game reads the virtual GamePad's motion exactly as with
  the GamePad: same camera code, the in-game Gyro switch, its stick dead zone (now the Pro
  Controller's right stick) and its per-frame calibration. With the source off nothing changes.
  With the Mouse aiming source, while the game aims, `02618604`'s first check (the Options → Gyro switch,
  `0271FC30` on options +5) reads as on (`@02618634`); the option itself, and so the save, stays as the
  player set it. The other sources keep the game's switch. With the mouse camera on, the mouse source
  waits (`motion::set_mouse_look`): the mouse camera adds the mouse's degrees to `CalcSubjectAngle`'s
  GamePad input itself (`mods/camera.cpp`, `@02506CEC`), see below.
- Hosts: `platform/input_sdl.cpp` (SDL3 sensors, on only for the controller source; a watchdog turns a
  controller's sensors off and on again when they stay silent for 2 s while the game window has the
  focus), `gfx/input.mm` (GameController.framework, macOS 11+; sensors that the system switched off
  are switched on again), `platform/mouse_sdl.cpp` and `mods/mouse.mm` (mouse).

## Troubleshooting

- **The gyro stops working after a while.** The log (`[gyro]` lines) says what happened: "no motion
  from controller N for 1 s" and "motion from ... again" (the samples stopped and came back; the
  window losing the focus does this too, SDL pauses controller events then), "turning its sensors off
  and on again" (the watchdog), "motion from controller N" (another controller took over), or "the
  right stick has rested at x, y for 2 s while aiming" (the game ignores the gyro while the right stick
  is outside its 0.1 dead zone: a drifting stick does this; raise **Controls → Controller → stick dead zone**).
  The Gyro window shows the same.
- **More detail:** `WWHD_GYRO_LOG=1` logs, twice a second and for every source, the raw gyro and
  accelerometer samples, the sample rate, the bias, the gravity direction and the aim, plus what the
  virtual GamePad gets; on the macOS app also the raw GameController values.

Debug: `WWHD_TEST_GYRO=from-to:yaw:pitch,...` turns the virtual GamePad by yaw / pitch degrees per
second during game-time seconds (any source, also off; no sensitivity; game time, so it does not
depend on how fast the machine runs).

## Testing with real hardware (wanted)

The axis modes are tested with simulated controllers (SDL and Cemuhook conventions, flat, tilted and
upright holds) and the game's reading with scripted game runs, but not yet with physical
controllers. Please report (with the log) if something is off:

1. **DualSense / DualShock 4 / Switch Pro** with **Controller gyro**: enter first person (R3) and
   draw the bow. In **Player space**, turning the controller right turns the view right and tilting
   its top up looks up, however you hold it; rolling does nothing. **Yaw** and **Roll** as described
   above. With the controller on a desk the view must stay still after a second (bias calibration).
2. Same on the **macOS app** (GameController.framework, a different axis mapping:
   `WWHD_GYRO_LOG=1` logs the raw values).
3. **Steam Deck** (SDL) and **Joy-Con** pairs.
4. **Cemuhook**: DS4Windows or BetterJoy with the server on, source Cemuhook; the Gyro window shows
   "receiving motion". Stop the server: the game must keep running smoothly. Android needs network
   permission for this, which the app does not ask for yet.
5. **Steam Input gyro to mouse**: source Mouse; in first person the pointer is captured and the
   controller's gyro turns the view; after leaving first person the pointer is free again.
6. **Pro Controller mode** (Input → Wii U Pro Controller): the same, with the game's Pro Controller
   controls.
7. Long sessions: the gyro keeps working (if not, the `[gyro]` log lines say why).
