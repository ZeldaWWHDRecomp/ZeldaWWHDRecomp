# Button prompt implementation notes

Developer reference retained from the original controller and glyph documentation. The player
guide remains in [button-glyphs.md](button-glyphs.md); this file keeps hook, adapter, and test notes.

## Player-facing prompt behavior

**Controls → Controller → Face buttons** chooses which physical button performs each Wii U action.
**Button prompts** chooses how the game's icons look. They work together with both presets
and custom face bindings. Original always keeps the game's icons.

| Face buttons | Xbox prompts, Wii U A / B / X / Y | PlayStation prompts, Wii U A / B / X / Y |
|---|---|---|
| By position | red B / green A / yellow Y / blue X | Circle / Cross / Triangle / Square |
| By label | green A / red B / blue X / yellow Y | Cross / Circle / Square / Triangle |
| Automatic | Uses the resolved face bindings | Uses the resolved face bindings |
| Custom | Uses each bound host face button | Uses each bound host face button |

**GameCube** shows a large green A, a small red B, grey X and Y, a purple Z, grey L/R and a
yellow C-stick. Its HUD face buttons follow the controller's classic arrangement. The action,
item art and word stay together when the layout changes. The item menu keeps its compact OK/Back
pair inside the Wii U menu area. The game's Plus cue is rendered as a compact Start button glyph.
The pictures are drawn by the port and do not use Nintendo artwork. **Automatic** selects this
look when a supported GameCube adapter is the active pad. You can also choose it for any pad.

The HUD cluster follows the face bindings: each button, with its item art and action word, is
drawn where its pad button sits (an Xbox pad's A is the bottom button, which is where the game
draws B). By position keeps the authored positions (roll right, sword bottom), by label puts A
bottom, B right, X left and Y top, and a custom set such as Xbox X for the sword puts the sword on
the left.

Face-action words use the mapped button's Xbox or PlayStation prompt colour across all layouts, independently of **Coloured face buttons**. With **Coloured face button text** off, they keep the game's own text colours (white). Original prompts always keep the game's text colours, so the setting changes nothing there. Custom bindings must use four distinct host face buttons; duplicate bindings or non-face inputs fall back to the complete original icon set because they cannot describe a four-button controller picture.

The item menu's OK/Back pair keeps the mapped buttons' relative directions while fitting the original menu area: unused vertical rows are removed, and a pair without the rightmost button shifts one position right. The top/bottom pair retains one-third of its empty middle row to give the labels more clearance. The pair stays compact throughout its exit animation; the separate gameplay guide retains the full diamond spacing.

A guide displaying exactly one face button keeps it where the game draws it, whatever its binding or icon style: a conversation's lone Next or End stays at the Confirm position instead of following the cluster's rearrangement. The retained button set keeps that placement stable through transitions; guides with additional buttons retain their normal layout.

Automatic prompts follow Xbox and PlayStation pads and use GameCube prompts for a detected
GameCube adapter. Other Nintendo and unknown pads keep the original look with By position or
custom bindings. By label retains its existing Xbox fallback for those families. Switching takes
effect immediately, including cached message text.

## Options

In the settings overlay's **Controls** tab (and the Controls window on macOS), saved in
`controls.json`:

| Option | Choices | Default |
|---|---|---|
| **Button prompts** | Automatic, Original, Xbox, PlayStation, GameCube | Automatic: follows the connected pad; By label retains its Xbox fallback |
| **Shoulder names** | Automatic, L / R / ZL / ZR, L1 / R1 / L2 / R2, LB / RB / LT / RT | Automatic: follows the button prompts |
| **Coloured face buttons** | on, off | on; affects button icons only, leaving mapped action-text colours enabled |
| **Coloured face button text** | on, off | on; off keeps the game's own text colours (white); no effect with Original prompts |

A fixed **Shoulder names** choice applies with any face-buttons setting. Automatic shoulder
names follow the resolved prompt style.

No Nintendo, Microsoft or Sony artwork or font is shipped: the colours are applied while the game
draws, the shoulder captions are composed at runtime from your own copy's HUD font, and the
PlayStation shapes are simple geometry (two strokes, a ring, a square, a triangle) drawn by the
port.

## How it works (technical notes)

Four function hooks (`tools/recomp/hooks_glyphs.txt`), plus wrappers around the existing aspect
(`0x028766CC`) and RTL text (`0x028785C8`) hooks:

- `0x0286E9F0` (`CharWriter::Print`) marks button glyphs (A/B/X/Y: U+E000–E003; L/R/ZL/ZR:
  U+E083–E086) in unused bits of the cached glyph quad's byte `+0x2F`. It selects mapped Xbox
  icon characters while retaining the original Wii U identity in that marker.
- `0x028F7C7C` (TextBox vertex generation) tints marked quads and swaps shoulder captions and
  PlayStation shapes to the composed atlas for the vertex build, then restores everything it changed.
- `0x02874D54` (`Picture::DrawSelf`) tints Xbox picture backgrounds (`P_A_00`, `P_B_00`, `P_X_00`,
  `P_Y_00`, `P_ASpecial_00`) and temporarily swaps the first material texture for GameCube A/B/X/Y art.
  The swap and tint use `shared_patch` so concurrent GamePad item-menu draws restore their original
  material and vertex colours after each picture.
- `0x026F8CD0` (`hdParts_c::calcLayout`) scopes compact pair placement to `hdCommandButtonGuide_c` with a retained A/B-only face set (`mCachedButtons`), and keeps a single face in place in either command-guide class with exactly one displayed button. That set stays unchanged during its exit animation, even after the scene has returned to gameplay. Other layouts keep their own placement; no previous-frame state or timeout is needed.
- `0x028785C8` (`lyt::TextBox::DrawSelf`, the existing RTL wrapper) also recognises
  `CommandA_00`'s floating action hint: `T_A_00` and `P_A_00` are **siblings** under
  `N_Center_01`, unlike the cluster's parent/child pair. The same check supports B/X/Y
  with matching sibling pictures and a single matching letter. `T_A_01` on
  `P_ASpecial_00` covers the special-A prompt in both command layouts. These are plain
  `CKingMain` letters, so `CharWriter::Print` does not mark them as U+E000 icons.
  The scoped face identity feeds the existing `0x028F7C7C` vertex hook (bare PS shape,
  coloured RGBA or plain coverage); Xbox colours and GameCube picture silhouettes use `Picture::DrawSelf`. Xbox HUD
  text is temporarily changed to the mapped letter during a rebuild, then restored, because
  the direct font path can bypass `CharWriter::Print`. Action-word panes are scoped to their
  matching button pictures and tinted during vertex generation, with alpha retained.
  The HD screen uses `hdParts_c::draw` (`0x026F8B54`) → `drawLayout`
  (`0x026F8D08`) → the NW4F layout traversal → `TextBox::DrawSelf`.
  No new recompiler hook is needed. The wrapper returns directly to the original draw
  before pane recognition while the session gate is closed.
- The text-box draw wrapper recognises the HUD's shoulder captions (`T_L_00` … `T_ZR_00`) and face
  letters (`T_A_00` … `T_Y_00` on their `P_?_00` pictures), which the game prints as plain text.
- The pane-matrix hook moves the complete CommandGuide cluster (`N_All_00`) for the Xbox positions;
  only the computed global matrices keep the new layout, the authored locals are restored.
- `Pane::Draw` (`0x02877100`, the existing aspect hook) draws that cluster with its action words
  (`T_CommandA_00` … `T_CommandY_00`) held back and draws them after its last child, with the same
  DrawInfo, whenever a restyled look is active: the game draws the groups in a fixed order, so a
  moved group could otherwise cover a neighbour's word.

**Two threads at once (#326):** in the game's "play on GamePad" mode the item screen is drawn on two
threads at the same time, so every temporary change above (colours, positions, glyph quads, the HUD
letter) goes through `runtime/src/shared_patch.h`. The first thread to change a value keeps the
original on the host, later threads change it from that original, and the last one out restores it,
unless the game wrote the value meanwhile. A plain save and restore let the second thread keep the
first one's change as the original, which made the Xbox tint permanent, a little darker each time.

**Untouched until used:** none of the hooks write to game memory until a different look has been
selected once in the session; the first activation rebuilds each cached text box once so its glyphs
get marked. A loaded save state, or a change from one look to another (colours, shapes, shoulder
names), clears that rebuild list. The look is resolved once per controls change or new controller
family (`input_map::prompt_style`), so drawing costs nothing extra per frame.

**Why the marker bits are safe:** each cached glyph quad is 0x30 bytes, and its last word (`+0x2C`) is
only touched by three functions in the game code: the emitter `0x0286E4FC` writes the 16-bit value at
`+0x2C` and the page byte at `+0x2E`, stores zero to `+0x2F` and then sets only bit 0 (a coverage
glyph of an alpha font, which takes the text colours; the icon font's RGBA glyphs have it clear);
batch grouping `0x028F7A4C` reads `+0x2F` masked with `& 1`; vertex generation `0x028F7C7C` reads
`+0x2C`/`+0x2E` and never `+0x2F`. Bits 1–7 of `+0x2F` are therefore free, and every new build of a
quad clears them.

**Shoulder captions and shapes:** the characters L, R, 1, 2, B, T are read from the player's
`CKingMain` font through the game's `Font::GetGlyph`; its BC4 (or plain alpha) sheets are decoded on
the CPU and composed into a small host-side atlas (128×1024, keyed by font) next to the PlayStation
shapes (`button_glyph_art.h`). If the font isn't available, the original labels and letters are kept.
The atlas is an RGBA sheet like the game's icon font: its sheet byte `+0x0E` and the quads' bit 0
stay clear. With them set, the batch (`0x028F7A4C`) takes the text-colour path, which maps the
texture through the text box's colours: a dialog's white text then drew the framed label as a white
box (#141). Two kinds of HUD quad use a coverage twin of the same sheet (`+0x100`, sheet byte set)
and keep bit 0 set instead: the plain PlayStation shapes and the bare L1/R1/L2/R2 (LB/RB/LT/RT)
captions on the HUD's shoulder plates. The game then draws them in the colours of the letter they
replace, including the dimming of an unassigned button; as RGBA pictures the white captions were
unreadable on the white shoulder plates (#143).

The hook behaviour is covered by `runtime/tools/button_glyphs_test.cpp` (synthetic guest memory: face
and shoulder marking, colour restoration, live switches, the untouched-until-used gate, *Automatic*,
HUD positions including 21:9, font fallback, every option, the PlayStation shapes, CommandA sibling letters for all
four faces in coloured/plain PS and Xbox/Original looks, and special A);
`runtime/tools/input_map_test.cpp` covers the option resolution and the `controls.json` keys
(`button_prompts`, `shoulder_names`, `button_colours`). The EU build maps these hooks. After a change to the
hook list, regenerate the game code (setup does this on update).
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
