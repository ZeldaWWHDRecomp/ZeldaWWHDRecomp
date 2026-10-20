# GamePad screen modes

Choose a mode in Display → GamePad screen (or set `WWHD_DRC_MODE` for one launch):

| Mode | Behavior |
| --- | --- |
| `window` | GamePad picture in a separate window. If that display is unavailable, it appears in the TV window. |
| `pip` | GamePad picture in a corner of the TV window. |
| `auto` | GamePad menus, and the song list while Link conducts with the Wind Waker, appear in PiP while open and disappear about 250 ms after closing. The game’s Off-TV Play switches the main picture to the GamePad; returning to TV restores automatic menus. |
| `off` | GamePad picture hidden; the game continues drawing it. |
| `gamepad` | GamePad picture fills the main window. Use the game’s Minus button for Off-TV Play. |

Auto follows the game’s item, collection/pause, map/sea-chart, bag and prompt state. Normal gameplay
and ordinary cutscenes do not open PiP. Menus routed to the TV in Pro Controller mode do not open it
on the GamePad. The port only reads this state and does not open, close or alter the game’s menus.

Cmd+G (Ctrl+G with the SDL host) toggles the **pin** in auto, including while a menu is already open.
The pin keeps PiP visible after closing a menu. During an Off-TV transition the pin is retained;
the full-size GamePad picture takes precedence until the game returns to TV. In other modes the
shortcut keeps its usual show/hide behavior. PiP corner, size, opacity and touch mapping are shared
with manual PiP.

## Picture-in-picture placement

Settings → Display → **Picture-in-picture placement** sets where the GamePad picture sits and how it
looks. The game picture shows each change while this section is open.

- **Position:** pick one of nine spots (or Custom) from the list, or use the small screen diagram.
  Drag the picture in the diagram, or the outlined picture over the game, to put it anywhere; it
  doesn't jump to a spot while you drag. To use one of the nine spots, click or tap its circle in
  the diagram. With a controller, focus the diagram and move the picture with the D-pad.
- **Size:** 5 to 100 % of the TV picture's width. At 100 % it covers the whole TV picture; at the
  top-left, top-right and bottom-right spots it may stay a little smaller so the game's buttons
  and rupee counter remain visible.
- **Opacity:** 10 to 100 %. To hide the picture, turn off *Show GamePad screen* instead.

Use the sliders for both; Ctrl+click a slider to type a number. Settings saved by an older
version keep their values.

The optional `WWHD_DRC_PAUSE=1` view (default on Android) still shows a paused menu full size after
Plus in `pip` or `off`, then restores the previous view when that menu closes. It now reads menu
state too. Auto always uses PiP for menus. Choosing a view manually cancels a temporary pause view.

Auto no longer measures brightness or picture changes. `WWHD_DRC_AUTO` and `WWHD_DRC_AUTO_LOG`
have been removed. No GPU sampling, signature textures or signature readback is used. Screenshots
and present dumps retain their own capture path.

## Steam Deck Gaming Mode

In Gaming Mode, the GamePad screen defaults to **Picture-in-picture** and the game starts
in **Full screen**, unless you already saved a choice or supplied a launch override.
This also applies to the Windows build through Proton when the gamescope session is detected.
Desktop Mode keeps its existing defaults.

Change **View** under **Settings → Display → GamePad screen**, or change **Full screen**
in Graphics. Your choices are remembered. A separate GamePad window is still available,
but Gaming Mode shows one window at a time and may scale the game picture, making it blurry.

The old launch-option workaround to hide the GamePad and force full screen is no longer
needed for a fresh configuration. Remove those launch options to use picture-in-picture;
existing launch overrides still take precedence. The aspect default is unchanged: choose
**Match window** or **16:10** to use the Deck screen's shape.

## State source and implementation

The reader runs on the guest thread at `GX2SwapScanBuffers`, before the frame is queued. Addresses
below are canonical USA addresses and are translated through `GD()` for regional builds:

- `101F8344`: SCENE-ingame_ui singleton. Its state machine at `+10` has a current state object at
  `+8`, whose descriptor is at `+8`; without a current object, the pending descriptor is at `+4`.
  This mirrors the read-only getter `02006478` without calling guest code.
- The scene’s item screen at `+17C` has a bag window selector at `+54`: `0..2` are the bag
  windows and `3` is closed. This covers bags opened inside the ordinary Play state too.
- `101F8348`: the UI manager’s screen routing status. `2` denotes the DRC menu overlay; `4` also
  occurs in cutscenes and must not, by itself, trigger PiP.
- `101F5088`: hdInputManager singleton; `+1D0` is controller routing. `1` is GamePad with TV
  gameplay, `2` is alternate GamePad / Off-TV, and `0` is Pro Controller / TV UI.
- Scene descriptors `1049EC84/1049ECB0/1049ECDC` cover menu entry, menu and subwindow, including
  items, collection, map and Delivery Bag. Save/options, song lists, warp map, photo lists, Fishman
  hints and password entry have explicit descriptors listed in `runtime/src/gfx/auto_pip.h`.
- Wind Waker: play object `1046F0B0` (`dComIfGp_get`) byte `+5BD1` is the metronome flag that
  `daPy_lk_c::procTactWait_init` (`023E92A0`) sets while Link conducts and the song's end or a
  cancel clears; scene descriptors `1049EDB8/1049EDE4` are the GamePad song list opening and open
  (it trails the flag by a few frames on both ends). Either one shows PiP with GamePad routing `1`.
- `1049F498`, `1049F4C4`, `1049F4F0` identify TV, DRC and Pro mode exchanges. These clear the closing
  delay and suppress PiP while routing changes; the pin survives.

The state is published as atomic host-only flags. The renderer does not read guest memory. There
are no guest writes or injected inputs. Outside auto, the reader exits before guest reads unless
the existing optional Plus pause view is pending or active. The former GPU sampling and waits are
gone from both Metal and Vulkan. The luma functions used by FXAA remain part of antialiasing.

Unit tests: `auto_pip`, `auto_pip_pause`, `auto_pip_eu`, `gamepad_ui_state`, `display_fallback`, and `stereo_pointer`.
The policy tests use both the identity map and the shipped EU map. The reader test maps a synthetic
regional guest heap read-only, with shifts changing inside the descriptor table, and exercises every
menu and switch descriptor with routing shortcuts disabled, as well as cutscene,
Off-TV, transitions, scene destruction and inactive modes; a guest write would fault.

For scripted capture diagnostics, `WWHD_TEST_GAMEPAD_UI_TRACE=1` logs changes to the scene descriptor,
screen routing and Off-TV mode. It does not measure the pictures.
