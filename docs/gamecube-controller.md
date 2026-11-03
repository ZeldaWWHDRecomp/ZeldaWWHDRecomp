# GameCube controller layout

Open **Settings → Controls** and choose **GameCube** under **Face buttons**. To change one
controller only, select it under **Bindings for** first. **All controllers** changes the shared
bindings used by controllers without their own profile. The game's Controls page also has a
GameCube layout toggle for the controller in use.

## Wii U GameCube adapter on Windows

The adapter needs the WinUSB driver, the same one Dolphin uses: install it with
[Zadig](https://zadig.akeo.ie/) for the device "WUP-028" (USB ID 057E 0337). Windows' own driver
for the adapter isn't supported. If the adapter already works in Dolphin, nothing else is needed.

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
| Left | Change the GamePad page, when **L turns the GamePad page** is enabled |
| Right | Open Settings (the default shortcut) |

Start opens the in-game menu when released on its own. A direction used with Start is consumed by
that shortcut until released, so it will not also move Link or change a menu selection.

L and R trigger their assigned actions at 25% travel or when clicked: L targets, while R uses the
shield. The layout preserves the game's usual camera options; adjust horizontal camera
direction in the game's options if you prefer the original GameCube feel.

## Button prompts

The controller layout and the on-screen button style are separate choices. In **Controls → Button
prompts**, choose **GameCube** to show the classic green A, red B, grey X/Y, purple Z, grey L/R,
yellow C-stick and Start button. **Automatic** uses this style when a supported GameCube adapter
is recognized. Choose it by hand for another controller. See [Button prompts](button-glyphs.md)
for the other styles and colour options.

In GamePad mode, the map and item touch pages remain on the GamePad screen. Choose Wii U Pro
Controller in the game's controller options to use controller-driven menus.
