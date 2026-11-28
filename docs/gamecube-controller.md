# GameCube controller layout

Open **Settings → Controls → Controller** and choose **GameCube** under **Face buttons**. To change one
controller only, select it under **Bindings for** first. **All controllers** changes the shared
bindings used by controllers without their own profile. The game's Controls page also has a
GameCube layout toggle for the controller in use.

## Wii U GameCube adapter on Windows

The adapter needs the WinUSB driver, the same one Dolphin uses: install it with
[Zadig](https://zadig.akeo.ie/) for the device "WUP-028" (USB ID 057E 0337). Windows' own driver
for the adapter isn't supported. If the adapter already works in Dolphin, nothing else is needed.

## PC-mode adapters on macOS

PC-mode GameCube adapters (Mayflash-style, often named "GameCube Controller Adapter", USB ID 0079 1843,
1844 or 1846; set a Mayflash adapter's switch to PC mode) are read directly on the Mac, in the layout SDL uses
for them: GameCube A, B, X, Y, Z, Start and the D-pad, both sticks over their full range, analog L and R and
their clicks. macOS's own GameController description of these adapters has the wrong buttons and stick ranges
(issue #556), so the game leaves it out. Each adapter port in use is a controller of its own, listed as
"GameCube Controller Adapter (port 1)" and so on, and gets the GameCube layout below automatically.

The sticks calibrate themselves: the centre is where a stick rests when the adapter starts reporting, so
leave the sticks and triggers alone while plugging it in; a stick that reaches further than usual
widens its range the first time it does. Nintendo's own adapter (WUP-028) isn't read this way on the Mac.

To check what an adapter reports, turn on **Settings › Graphics › Bug reports › Write a log file**, quit
the game, plug in the adapter and start the game from Terminal with `WWHD_HID_DUMP=1`:

```sh
open -a "Wind Waker HD" --env WWHD_HID_DUMP=1
```

The log (`captures/wwhd.log`) then lists the adapter's HID elements once (usage page, usage, logical range)
and its raw reports and element values as they change, a few lines a second at most.

## Main controls

| GameCube control | In-game action |
| --- | --- |
| A | Context action, roll, talk and confirm |
| B | Sword and cancel/back |
| X | Item slot 1 |
| Y | Item slot 2 |
| Z | Item slot 3 |
| L | Target and center the camera |
| R | Shield and crouch |
| Control stick | Move and navigate menus |
| C-stick | Camera, aiming and baton |
| D-pad | Navigate menus and use the game's shortcuts |
| Start | Open the items, collection and map menus |

The Picto Box is an item. Assign it to X, Y or Z, then use the assigned button. The optional item
ring uses the same item buttons; it does not need a separate binding.

## Extra controls

The GameCube controller has fewer buttons than the Wii U controls, so hold **Start** and press a
direction for these extra actions:

| Hold Start and press | Action |
| --- | --- |
| Up | Enter first-person view |
| Down | Switch to Off-TV Play |
| Left | Change the GamePad page (L) |
| Right | Open Settings (the default shortcut) |

Start opens the in-game menu when released on its own. A direction used with Start is consumed by
that shortcut until released, so it will not also move Link or change a menu selection.

L and R trigger their assigned actions at 25% travel or when clicked: L targets, while R uses the
shield. The layout preserves the game's usual camera options; adjust horizontal camera
direction in the game's options if you prefer the original GameCube feel.

## Button prompts

The controller layout and the on-screen button style are separate choices. In **Controls → Prompts → Button
prompts**, choose **GameCube** to show the classic green A, red B, grey X/Y, purple Z, grey L/R,
yellow C-stick and Start button. **Automatic** uses this style when a supported GameCube adapter
is recognized. Choose it by hand for another controller. See [Button prompts](button-glyphs.md)
for the other styles and colour options.

In GamePad mode, the map and item touch pages remain on the GamePad screen. Choose Wii U Pro
Controller in the game's controller options to use controller-driven menus.
