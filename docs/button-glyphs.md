# Button prompts

Choose a button style in **Controls → Prompts → Button prompts**. The setting works with the GameCube
controller layout, other controller layouts and custom button bindings.

| Style | What you see |
| --- | --- |
| **Automatic** | Prompts that match the connected controller. A recognized GameCube adapter uses the GameCube style. |
| **Original** | The game's usual buttons and colours. |
| **Xbox** | Xbox-style letters and colours. |
| **PlayStation** | Cross, circle, square and triangle. |
| **GameCube** | A large green A, small red B, grey X and Y, purple Z, grey L/R, yellow C-stick and a Start button. |
| **Keyboard / mouse** | The assigned keyboard or mouse inputs on HUD, item-menu and dialog face/shoulder prompts. |

**Face buttons** is a separate setting. It chooses which controller buttons perform the game's
A, B, X and Y actions. **Button prompts** chooses how those actions look on screen. You can use
GameCube prompts with any controller or binding.

The GameCube style arranges the HUD buttons like a GameCube controller: A is large and to the
right, B is down-left, X is to the right of A, and Y is above A. An action, its item and its word
stay together when they move. The GameCube artwork replaces the original button plates, and
its labels remain visible in front of large item pictures. Messages and menu prompts use the
same button style. The item menu's OK and Back prompts stay close together to fit its Wii U
layout, and the item ring keeps its existing layout.

**Coloured face buttons** controls the button pictures. **Coloured face button text** controls
the colours of action words such as “Roll” and “Attack,” including GameCube shoulder actions
such as “Defend.” Turn the text option off to keep the game's usual word colours. Original
prompts always keep the game's usual colours.

**Shoulder names** can follow the prompt style automatically or use Nintendo, numbered or Xbox
labels. GameCube prompts use L, R and Z. Your choices are saved.

## Where to change it

Open **Controls → Prompts** in Settings, then choose **Button prompts**. On macOS, the same choice
is available in the Controls window. Changes apply to messages and the HUD as well as menus.

## Keyboard and mouse bindings

Choose **Controls → Prompts → Button prompts → Keyboard / mouse** to replace A/B/X/Y and
L/R/ZL/ZR prompts in the HUD, item menu and dialog text with keycaps. Native
Plus/Minus and individual D-pad/stick direction icons show their mapped keys too.
Labelled left/right-stick and D-pad icons show the four directional keys in a cross.
The unlabelled stick glyph retains its native artwork because it does not identify a stick. This is an
explicit choice; Automatic continues to follow the controller. The first assigned
keyboard/mouse slot supplies the caption, with the second used if the first is empty.
Bindings update immediately. `M1`–`M5` identify mouse buttons and `Wh+`/`Wh-` the wheel;
`None` means the action has no keyboard or mouse binding. Key names use the same
physical-key identities as Controls. The game font supplies all lettering.

Keyboard prompts take priority over the item ring's A1/A2/A3 captions. Controller
colour/placement settings remain saved and apply again when a controller style is chosen.
Mouse camera, Direct right-stick camera and First-person shortcut now live in Controls,
along with camera speed and mouse sensitivity. Existing saved settings still apply.
The existing first-person mouse-wheel behavior is retained.

If the game font cannot supply a caption, that keycap shows a question mark (or a
crossed keycap if the question mark is also missing), and the runtime logs the font
and first unavailable caption. Other bindings still render. Text that would exceed
the game's eight texture batches retains its native icons until its contents or
bindings change; it does not rebuild every frame.
