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

## Setting up a controller, and buttons per controller

The first time a new controller is used, Settings opens with **New controller: Set up / Later /
Don't ask again**. **Controls > Set up controller...** runs the same setup any time:

1. **Face buttons**: *by position*, as on the Wii U GamePad (the bottom button is the sword, the
   Wii U's B; the right one the action button, A), or *by the letters on the buttons* (the button
   marked A is the action button).
2. **Layout**: *Original* (every button as on the Wii U; Jump anywhere and the moon jump on L1);
   *Jump on an item button* (that item can't be used from the controller, the other two can); or
   *Jump on any button, its use moved to L1* (what the button did moves to L1, so nothing is lost).
   Both jump layouts switch Jump anywhere on.
3. **The jump button** (the two jump layouts): press it on the controller.
4. **Try it**: every press shows what it does in the game. Hold **Start** to finish, hold
   **Select** to change the setup, so the setup works without touching the screen.

The controller then keeps **its own buttons**. **Controls > Bindings for** chooses which buttons the
drawing (or the list) edits: *All controllers* (the shared buttons every other controller uses) or
one controller, with **Give it its own buttons** / **Use the shared buttons**. The keyboard is always
shared. A controller is recognised by its SDL GUID and name, then by its GUID alone (the same model
under another name), then by its name alone (the same model on another connection, and the macOS
app, which has no GUIDs).

**Controls > Controller details...** lists what the system reports about each connected controller:
vendor and product IDs, GUID, connection, battery, rumble motors, motion sensors, lights, buttons and
axes, which buttons it uses, and what is pressed right now. On Android some controllers have motion
sensors or rumble that apps only get with the right Linux kernel driver: a DualSense or DualShock 4
needs hid-playstation (Linux 5.12 or newer), Nintendo's Pro Controller and Joy-Cons hid-nintendo
(5.16 or newer). Without it they work as plain gamepads; the details page and the Controls tab say
so. No app can get past that; a system update or a kernel with the driver brings them.

**Rumble on** (Android): *Automatic* (default: this device while you play by touch, the controller's
motors otherwise), *Automatic, this device for a controller without rumble*, *This device*,
*Controller* or *Both*. The Gyro dialog's **Automatic** source uses the controller's gyro while you
play with it and this device's while you play by touch.

## Jump anywhere and the moon jump on inputs of their own

Jump anywhere and the moon jump cheat have their own inputs in Controls (the **Jump anywhere** and
**Moon jump** row): a controller input and a key each, L1 and Q by default. They never press a Wii U
button themselves. On one input the moon jump wins while it is on. On a touch screen each has a
button of its own while it is on, hidden in cutscenes, talks, menus, the Picto Box and the telescope
(and Jump also in water, on ladders and walls, where it does nothing).

Choosing a controller button the game uses asks what happens to that button's own use: **Move** (to
L1, else d-pad down: the game uses neither while you play), for an item button **Move to L1** or
**Move to d-pad down** (the item goes there), or **Take off** (no controller button presses it any
more; for an item button, that item can't be used from the controller). An item button on L1 counts
in menus too (L1 no longer turns the GamePad page); on d-pad down it is the item button only while
you play, d-pad down in menus.

**L turns the GamePad page** (Controls, off by default): in play the game reads L only to turn its
GamePad screen between Items and Map. Off, L reaches the game only outside play, so L1 can jump
without turning the page. Turn it on for a GamePad picture on a second screen. It applies the same
way with **Wii U Pro Controller** chosen in Controls.

Controls files from before these options (format 1) keep working: Jump anywhere and the moon jump
take the inputs that pressed the Wii U's L, and the old Jump anywhere button choice (R, ZL, ZR, L3,
R3) becomes the Jump input once. Saving writes `"version": 2` with `controllers` (each controller's
own buttons by `guid` and `name`), `controller_setup_declined`, and `l_turns_gamepad_page` (only
when on).

## The item ring

Hold **X**, **Y** or **R** while you play and a ring of your items opens. Tilt the left stick towards an
item (on a touch screen: slide the finger) and let go of the button: the item goes on that button, as
in the game's item screen. An item already on another button swaps places with the old one.

- **Bags** (spoils, bait, delivery) are not put on a button: rest on one for a moment and it opens as a
  ring of its contents; rest on the arrow to go back.
- The **Wind Waker** makes Link conduct at once (the game's d-pad up); it doesn't take the button.
- On the **boat** the ring also has the cannon (with bombs) and the salvage arm (with the grappling
  hook).
- The **empty** entry (a struck-through circle) leaves the button empty.
- Let go without pointing anywhere and nothing changes.
- A short press still uses the item, a moment (0.2 s) later, when you let go. While Link aims or uses
  an item (bow, hookshot, boomerang, telescope, Picto Box, Deku Leaf, conducting, the sail), and outside
  play, the buttons work at once and the ring doesn't open.

Controls → **Item ring**: **Touch only** (default: the on-screen X, Y and R), **Touch and controllers**,
or **Off**. It is off for controllers by default because of that short wait on every press.

**While the ring is open** the game runs on, runs in **slow motion** (default: a third of its speed) or
is **paused** (the game's own short pause, as when a strong hit lands; the music plays on).

**Item button names**: X Y R, A1 A2 A3, or **Automatic** (default: A1, A2, A3 while you play by touch).
The ring, the game's HUD buttons and the GamePad's item slots show them.

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
