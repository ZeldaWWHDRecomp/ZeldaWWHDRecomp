# Controller directions and Wind Waker controls

The physical controller layout and the emulated Wii U controller are separate
choices. Controls selects whether your inputs act as a Wii U GamePad or Pro
Controller. Select that controller in the game's Controller Selection screen too.
The face-button preset changes A/B/X/Y; it does not change D-pad or stick directions.

**Stick dead zone** defaults to **15%** for both controller sticks. This filters
small resting-stick drift, which can otherwise keep the Picto Box zooming and its
picture blurred. Older controls files with the former 0% default automatically
load as 15%; other saved values are kept. Saving controls writes an
`options.format` marker of `2`, so a deliberate 0% setting saved in the new format
is preserved on subsequent loads.

**Stick range** is set per stick (**Left stick range**, **Right stick range**),
from 100% to 150%; the default 100% changes nothing. Raise it if a stick can't
reach full deflection, for example when the Wind Waker baton's directions don't
register because the game needs an almost fully tilted stick. The range applies
after the dead zone: the stick's tilt is multiplied by the range and capped at
full deflection, keeping its direction, so diagonals keep their angle. A stick
that only reaches 90% needs about 112% to reach full deflection. Both sliders
sit below the dead zone in the Controls tab and in the macOS Controls window, take
effect immediately and are saved as `left_stick_range` and `right_stick_range`
in the controls file's `options` (stored as factors, 1.0 to 1.5). Files without
these keys load at 100%; values outside the range are clamped.

**Invert camera up/down** inverts the camera only: the follow camera, first-person
views and item aiming (bow, hookshot, boomerang, grappling hook, telescope, Picto
Box) and the mouse camera. It is applied where the game's camera reads the right
stick, so the stick itself is not changed: the Wind Waker baton, the Nintendo
Gallery figure viewer's zoom and any other right-stick control keep their normal
directions. The game also has its own setting for the vertical direction of
first-person aiming; it still works, and with both inverted they cancel out in
first person.

While conducting, the **right stick supplies the notes**, including down. The
**left stick controls the meter**: hold left or right to change the beat count.
The D-pad does not substitute for the note stick in the original game. For example,
the Song of Passing uses right, left, down on the right stick in three-beat meter.

D-pad up/down can move the Quest Log cursor in File Selection in both controller
modes. D-pad directions also navigate the pause-menu sidebar and item grid in
both modes. In GamePad mode, the inventory appears on the GamePad screen. Other
screens have their own controls; follow the game's stick/button prompts. The port does not turn D-pad presses into stick movement automatically.
If down fails, check its binding in Controls and test D-pad down, Left Stick Down
and Right Stick Down separately.

## Controllers while the game isn't focused

Controls → **Controller input when the game isn't focused** keeps your controllers playing the game
while another window has the focus, for example with the game on a second screen, while streaming
or with remote play. It is off by default and saved with your controls. The change takes effect at once.

- Only controllers are affected. Keyboard keys and mouse buttons are still released when the
  game loses the focus, and they reach the game only while it is focused again.
- Rumble keeps working in the background on Windows and Linux. The macOS app does not drive
  controller rumble yet.
- A controller press in the background never opens Settings. If Settings is already open, the
  controller can still navigate and close it. Other controller shortcuts work as they do with
  the focus.
- On macOS this needs macOS 11.3 or later.
- With Steam Input turned on for the game, Steam may still send controller input only to the
  focused window. Turn Steam Input off for the game if background input does not work.
- Android does not have this option: the app is either in front or paused. Handhelds with a
  built-in controller are not affected.

## Mouse and keyboard

On Windows, Linux and macOS, click a key or alternate-key field in **Controls**,
then press a mouse button or scroll the wheel. The same slots accept keyboard
keys, so you can keep a keyboard alternative. Mouse inputs are unbound by default.
Their stable IDs in the `keyboard` section of `controls.json` are:

| Input | ID |
| --- | --- |
| Left mouse button | `Mouse1` |
| Right mouse button | `Mouse2` |
| Middle mouse button | `Mouse3` |
| Side buttons 4 and 5 | `Mouse4`, `Mouse5` |
| Wheel up and down | `WheelUp`, `WheelDown` |

For example, `"B": ["J", "Mouse1"]` keeps J and also binds left click to B.
Existing keyboard/controller files load unchanged. Each wheel notch supplies one
short press, with a release between successive notches; fractional trackpad
scrolls accumulate to a notch.

Enable **Mouse camera** and click the TV picture to capture the pointer. Move the
mouse to look around on foot or while sailing, at the same mouse sensitivity. That
first click only captures; release it and click again to use its binding. Bound
buttons work only in the TV window while the mouse camera has captured the
pointer. Escape releases it; with the pointer free, Escape opens Settings. A left
click in the GamePad window remains a touch;
settings and text prompts take priority over game bindings. A button held during
these transitions stays blocked until released, without blocking other buttons
or other SDL mice.

**First person on the mouse wheel** takes priority over `WheelUp` and `WheelDown`.
Turn that option off to use wheel bindings. When left click is bound, it replaces
the mouse camera's automatic “fire the aimed item” action. Middle click releases
the pointer only when `Mouse3` is unbound; with it bound, use Escape to release.
On macOS, unbound side buttons also retain their pointer-release behavior.
The mouse gyro still receives movement independently of button bindings.

A useful manual setup is left click = B, right click = R, and middle click =
right-stick click (first person, with that mod enabled), with WASD and the remaining keyboard bindings.
No preset is applied automatically. A mouse input assigned to Screenshot keeps
the existing screenshot-shortcut priority over game actions sharing that input.

## Nintendo controllers on macOS

Switch Pro, paired Joy-Cons and SNES-style Nintendo controllers use physical
button positions for **By position (Nintendo)**. **Automatic** follows their
printed letters and keeps the original Nintendo prompts. Single Joy-Cons use
their sideways face-button positions.

If you previously swapped A/B and X/Y by hand to work around the macOS issue
(#181), select **Input > Face buttons > By position (Nintendo)** or **Automatic**
once after updating. The old workaround appears as **By label (Xbox)**; saved
manual bindings are preserved because that preset may be intentional or shared
with another controller. A connected Nintendo pad with that exact manual pattern
logs a reminder. Other custom bindings are kept.

## Input diagnostics

For a reproducible controller report, these optional environment variables log
the packets written for the game:

- `WWHD_VPAD_TRACE=path`: GamePad reads.
- `WWHD_KPAD_TRACE=path`: Pro Controller reads on channel 0.

Each line contains `logic_step repeat hold trigger release lx ly rx ry`.
Button fields are hexadecimal; sticks are signed decimal values, with positive Y
pointing up. `repeat=1` identifies an interpolation/repeated input sample. The
existing four-column `WWHD_PAD_TRACE` format is unchanged.

D-pad down is `00000100` in VPAD and `00004000` in the Pro Controller's extended
button fields. In Pro Controller mode, the GamePad remains connected for its
screen and touch, but its buttons and sticks are idle. Read the KPAD trace for
controller input in that mode.

The game-free `sdl_input` test drives virtual Xbox and Switch Pro gamepads through
SDL polling and the configured bindings. `pad_delivery` checks the actual VPAD
and KPAD writers, including direction bits, press/release edges, both sticks,
interpolation repeats and fresh analog samples. Neither test reads game data or a save, or uses a physical controller. These
checks do not replace testing a particular physical device and its driver.

Controls → **Skip controller selection** is on by default. At the title screen it chooses the
configured Wii U GamePad or Pro Controller through the game's normal selection path; turn it off
to choose manually. Unavailable options and controller-disconnect prompts remain interactive.

## App shortcuts and extra buttons

Open **Settings → Shortcuts** to change app actions. **Settings** shows Home as its default
controller button. You can replace it with another button or a hold-and-press combination.
The key, alternate key and controller binding are saved together. Esc (or F1) opens Settings.
With a captured mouse, the first Esc frees the mouse and a second one opens Settings. Inside
Settings, Esc closes it when you are not recording an input. If you already changed the
Settings key, your choice is kept; if you move Settings away from Esc, Esc still frees the mouse.

Select a controller field, then tap a button or hold one button and press another. During
recording, L/R, Select and Home are captured without switching tabs or closing the menu.
Capture/Share, rear paddles, touchpad clicks and other extra buttons can be assigned in both
Controls and Shortcuts; they start unbound. Extra buttons without a standard name appear by
number. Availability depends on the controller and its driver.

Right-click a binding to clear it. Controller users can focus it and press the button mapped
to X; keyboard users can press Delete or Backspace. The right mouse button remains assignable
while a field is recording. A combination takes only its two buttons, and only while both
are held. A single-button shortcut takes that button while held.
