# GameCube controller preset (#301)

Choose **GameCube** in Settings → Controls (or Input → Controls on macOS). Select the pad in
**Bindings for** first to make only that controller use the layout. The native macOS window now
has the same target selector. The Input menu changes the shared layout. The game's own Controls
page has a GameCube layout toggle for the controller in use; switching it off restores its old
bindings. Other controllers continue using their own profile or the shared defaults.

## Complete action table

| Wii U / port action | Physical GameCube input | Purpose / decision |
| --- | --- | --- |
| A | A | Context action, roll, talk, confirm |
| B | B | Sword, cancel/back |
| X | X | Item slot 1 |
| Y | Y | Item slot 2 |
| R | Z | Item slot 3: the original Z item now lives on HD R |
| ZL | L, 25% travel or click | Target / center camera |
| ZR | R, 25% travel or click | Shield/crouch; rope stop, boat jump/cruise |
| Plus | Tap and release Start | Items / collection / map menus |
| Minus | Hold Start + down | HD Off-TV Play |
| L | Hold Start + left | GamePad page; enable “L turns the GamePad page” for this during play |
| Home | Hold Start + right | Host Settings with the default Home shortcut |
| D-pad up | Up | HD baton shortcut |
| D-pad down | Down | Navigation; unused by unmodified HD play |
| D-pad left | Left | Navigation / ship cannon |
| D-pad right | Right | Navigation / ship salvage |
| Left stick click | Unbound | No physical equivalent; HD core play does not need it |
| Right stick click | Hold Start + up | Enter first person; aim with the C-stick |
| Left stick up/down/left/right | Control stick up/down/left/right | Movement / menus |
| Right stick up/down/left/right | C-stick up/down/left/right | Camera / aim / baton |
| Screenshot | Unbound on pad | Keyboard/app screenshot shortcut retained |
| Jump / Moon jump | Unbound on pad | Optional port actions; bind manually if wanted |

The Picto Box is an item, so assign it to X, Y or the third slot (physical Z), then use that button.
The optional item ring also follows X/Y/Z: hold the item button and use its existing selection
controls. There is no new independent ring binding. In GamePad mode the map/items touch pages
remain in the GamePad window; select Wii U Pro Controller for the game's controller-driven menus.
The preset does not switch the Wii U controller type, change ring settings, or emulate touch taps.

## Reasons for the differences

Nintendo's original GameCube booklet, **Using the Controller** (pp. 8–9), **Main Screen** (p. 11),
and **Items** (p. 12) establishes A action, B sword, X/Y/Z items, L targeting, R defend, Start
subscreens, control stick movement, C-stick camera, and D-pad maps. Its p. 8 explicitly separates
light L camera centering from a full press for targeting. See the
[scan of the Nintendo booklet](https://www.gamingalexandria.com/gc/The%20Legend%20of%20Zelda%20The%20Wind%20Waker/The%20Legend%20of%20Zelda%20The%20Wind%20Waker%20-%20Cleartype.pdf)
(PDF page 8 has printed p. 8; imposed spreads are not in reading order).
Nintendo's [manual index](https://en-americas-support.nintendo.com/app/answers/detail/a_id/16961/~/nintendo-gamecube-and-game-boy-advance-manuals)
links the original, but its old PDF URL currently returns 404.
The [HD digital manual](https://csassets.nintendo.com/noaext/image/private/t_KA_PDF/manual-WiiU-The_Legend_of_Zelda_The_Wind_Waker_HD)
and Nintendo's [HD controls guide](https://www.guiasnintendo.com/2b_WIIU/zelda_ww_hd/zelda_ww_hd_sp/cont_basicos.html)
cover HD's ZL targeting, ZR defense and right-stick click for looking. The
[HD inventory guide](https://www.guiasnintendo.com/2b_WIIU/zelda_ww_hd/zelda_ww_hd_sp/inventario.html)
confirms X/Y/R item slots; its [ship guide](https://www.guiasnintendo.com/2b_WIIU/zelda_ww_hd/zelda_ww_hd_sp/cont_navegar.html)
confirms left cannon/right salvage.

* **Trigger threshold:** normalized travel >= 0.25, or SDL's misc3/misc4 digital click. Both stages
  produce the same ZL/ZR action; the full click cannot accidentally use an item or open a menu.
  Light L locks on because HD's digital ZL does not expose the original two-stage distinction.
  This is a deliberate comfort approximation, not a claim to recreate the analog GameCube engine.
* **HD extras:** Start is a modifier while held; a lone Start produces Plus once on release.
  Its 100 ms Plus pulse survives host polling faster than the game. Start + direction consumes both inputs. A direction held after the chord stays consumed until
  released. Gate state is per physical device, so another pad cannot complete a chord.
  Up selects the first-person action; down/left/right provide Minus/L/Home. This preserves all
  ordinary D-pad navigation and HD ship shortcuts. Consequently the old D-pad up map shortcut
  moves into Start's menu, rather than making up open a menu during menu navigation or conducting.
* **Camera:** C-stick directions pass through unchanged, including baton input. The preset
  preserves shared camera preferences. For the original horizontal camera feel, select inverted
  horizontal camera in HD's own options; do not invert the raw C-stick (that would invert baton
  conducting too). Vertical inversion remains the player's choice. The camera options are
  documented in Nintendo's [HD options guide](https://www.guiasnintendo.com/2b_WIIU/zelda_ww_hd/zelda_ww_hd_sp/opciones.html).
* **Prompts:** automatic prompts keep the game's original A/B/X/Y identities with GameCube.
  Shoulder prompts are still HD's L/R/ZL/ZR, so the third item slot is displayed as R, physically Z.
  Settings and the on-screen keyboard read the same per-controller A/B bindings as gameplay.

## SDL adapter coverage

No real controller/adapter was available. All hardware/mode entries below need a tester.
The implementation uses SDL's **physical positions**, not its localized printed-button labels:
SDL GameCube uses AXBY (south A, east X, west B, north Y). Legacy recognized PC mappings with no
GameCube type, and native Apple GameCube framework elements, use ABXY; the detection routine picks that table. A user's existing mappings are
never globally normalized or rewritten to fix an adapter.

| Adapter / mode | Detection and mapping evidence | Verification |
| --- | --- | --- |
| Nintendo WUP-028, 057e:0337 | SDL HIDAPI: physical A/B/X/Y, right shoulder Z, trigger axes, misc3/misc4 clicks | Synthetic SDL raw-button fixture; no hardware |
| Mayflash Wii U mode / compatible adapter presenting 057e:0337 | Same HIDAPI protocol if recognized by SDL | Database/driver only; no hardware |
| PC-mode compatible adapters 0079:1843/1844/1846 | SDL's DragonRise/EVORETRO IDs and per-platform database; GameCube type or legacy PC face table | Detection/table unit tests; no hardware or PC-mode raw fixture |
| Cyber Gadget 0926:8888, HuiJia 1a34:f705, Austgame 05e3:0681 | SDL classification/database | Detection/table only; no hardware |
| Nintendo Switch Online GameCube 057e:2073 | SDL Switch 2 HIDAPI and GameCube classification; axes and click convention | Driver only; no hardware |
| PowerA 20d6:a711 / PDP 0e6f:0185 | SDL identifies GameCube-style controllers | Database only; no hardware |
| 8BitDo / other receiver in Xbox or Switch mode | USB identity describes the receiver/emulated pad, not the attached GameCube pad | Manual selection only unless SDL explicitly reports GameCube type; no hardware |
| Native macOS GameController host | No SDL VID/PID API here: explicit GameCube product category/vendor name; framework A/B/X/Y retain their letter identities; preset uses its ABXY table | Native UI/pure tests; no hardware; adapters not exposed by GameController require an SDL-host build |

Sources inspected: SDL **release-3.4.10**
[GameCube HIDAPI](https://github.com/libsdl-org/SDL/blob/release-3.4.10/src/joystick/hidapi/SDL_hidapi_gamecube.c),
[gamepad mapping conversion](https://github.com/libsdl-org/SDL/blob/release-3.4.10/src/joystick/SDL_gamepad.c),
[device classification](https://github.com/libsdl-org/SDL/blob/release-3.4.10/src/joystick/SDL_joystick.c),
and [mapping database](https://github.com/libsdl-org/SDL/blob/release-3.4.10/src/joystick/SDL_gamepad_db.h).
In particular, SDL's GAMECUBE_LABELS hint distinguishes positional mappings from legacy letter
mappings. SDL already applies that conversion; applying it a second time swaps physical B/X.
Unknown devices are not guessed from the manufacturer or a friendly-name substring on SDL hosts.

## Persistence and automatic selection

Automatic follows the first non-virtual connected pad. With shared Automatic on, a recognized
GameCube with no saved matching profile receives a new per-controller profile. Shared non-face
custom bindings block this automatic creation too. Existing profiles, including GUID/name fallback
matches, always win. Disconnecting or reconnecting does not replace them. A custom binding after
preset selection remains custom. Keyboard shortcuts, calibration, camera and other options stay
shared as before. Profiles use the existing GUID/name identity rules (same-model adapter ports
may share an identity); serial/port-specific profiles are not introduced by this change.

`pad_preset` metadata stores the original pad bindings and Automatic flag beside the new layout
in controls.json, both for shared bindings and each profile. Restore survives restart. Selecting
GameCube twice keeps the original backup. Other face presets retain the backup for Restore.
Old files have no metadata and retain their old bindings. No files need to be deleted to reset it.

## Reporter verification

Ask #301's reporter for the adapter model/mode and a dev-build test: big A acts/confirms, small B
swings/goes back, X/Y/Z use three separate item slots, light/full L target, light/full R shield,
Start opens once on release, chords perform their HD action without opening pause, and C-stick
camera/aim/baton work in every direction. Then save a custom pad binding, restart/replug/switch
pads, and verify Automatic never replaces it. Test the GamePad map/items pages or Pro Controller
menus and the optional item ring in gameplay. Windows/Linux/Android and native macOS hardware
execution remain unverified; headless tests are not a physical adapter or full-game test.
