# Native map/HUD bridge v1

Implementation note for the HD minimap follow-up. The guest module ABI remains 1;
this adds optional services, not a serialized guest structure or pane ownership.

## Native identities and integration

All addresses are canonical USA v0, mapped with `GC` / `GD` for USA/EU. Other
builds, missing native entries, or replacement conflicts leave capability zero.
The native adapter is `runtime/src/mods/map_hud.cpp`; synthetic-memory tests run
that same adapter, not a separate implementation.

The in-game UI global `0x101F8344` identifies its main HUD (+0x178) and map
coordinator (+0x218). Main HUD child 4 is `L_CompassClock_00` / `CompassClock_00`;
its vtable is `0x100EE998`. The coordinator's MenuMap (+0x28) has vtable
`0x100FB8B8`. Both are checked before access. Stage identity, UI/HUD/map/layout
identity changes and destruction invalidate host ownership. Native destructors
`0x0264C4BC` and `0x026986C4` revoke before delegating, including address reuse.
Full state loading explicitly clears all host requests and published suppression.

Compass `toWait` (`0x0264CE34`) is a reset, not ownership: `waitExecute`
(`0x0264C544`) enters In while `sCompassActive` (`0x1047B093`) remains set by
sailing. The bridge never calls it. Instead the existing native pane draw hook (`0x02877100`, in `aspect.cpp`)
skips the exact leased compass layout root and its descendants. The investigated
parts draw boundary (`0x026F8B54`) did not intercept the visible composed layout
in the runtime fixture, so the implementation does not rely on that boundary. Native
Wait/In/Display/Out updates, layout calculation, clock/wind orientation and input
continue. No native field, animation frame, parent or transform is changed.
Revocation simply stops suppression; the next native draw uses current state.
Only an atomic root identity is published to draw workers. Host request state is
protected across actor logic and the native UI event path. Rectangle bounds enclose visible content of the compass
layout (part +0x44 → layout +4 → native layout +0xC), using bounded traversal
of its live global matrices and origin codes, translated to guest HUD coordinates.
Picture bounds are intersected with the TV screen and follow the native alpha
inheritance gates. Only native Picture panes (DrawSelf slot +0x94 = `0x02874D54`, verified from
Pane::Draw at `0x0287713C`) contribute bounds; full-canvas containers do not.
The +0x50/+0x54 part fields point to rotating needles, not the complete surrounds.

MenuMap `displayExecute` (`0x0269C238`) consumes normal input via
`hdMenuMapHandleEvent` (`0x0269B238`) before updating the carousel. Native
previous/next events 0x5D/0x5E set direction (+0xB8), outgoing (+0xB0) and selected
(+0xAC), at stores around `0x0269B2C4` / `0x0269B310`. The host queues an
explicit-index event and dispatches those same three state changes **after**
normal input, only while the native change phase (+0x90) is idle. Native input
that changed selection wins and cancels an uncommitted SDK request.

This is an explicit host event extension at the native dispatch boundary; it does
not synthesize global input, repeatedly step the carousel, reset the screen, or
call the child artwork selector. Native `updateChartChange` (`0x0269BDE0`) and
`startChartChange` (`0x0269BBA4`) then own outgoing art, selected caption, WAIT,
FINISH and processed/previous selection (+0xB4). A request made during an
animation waits until idle; the same owner can replace or cancel it then.

Validation uses native coordinator `findChart` (`0x02687A64`), obtained
(`0x0268A570`), opened (`0x0268A5DC`), and deciphering (`0x025B8308`, save +0xE4).
Native indices 0..48, sector 1..49 and native kinds in mask 0x167 are accepted;
type 0 must be deciphered. No raw archive order or displayed chart number is used.
These identities and offsets were identified in the game-derived MenuMap,
map-display coordinator, compass-clock, main-HUD and parts code, and were already
recorded in the minimap's `NATIVE-CALLS.md` investigation.

## Minimap integration note

The normal C helpers import only the existing `wwhd_setting_get` service. A
fixed 36-byte version-1 command under `native-map-hud.v1` (query type 7) dispatches
to the same host implementation. This follows the SDK's existing optional query
convention: old hosts return zero for an unknown key/type, and the helper returns
`WWHD_MAP_HUD_UNSUPPORTED` (-1). It also works when an older installer translates
the new ELF. No native pane pointers or unbounded payload cross this transport.

Read unsigned settings `hud.boat-compasses.version` and
`map.chart-selection.version`; missing/zero disables the respective feature.
Direct imports of the five services are additionally supported for other
bindings, with `WWHD_MAP_HUD_DIRECT_IMPORTS`. Translator 0.3 makes those imports
optional on older runtimes; older translators do not. Prefer the default C
helpers for minimap compatibility.
Once per Link/actor logic step, call `wwhd_hud_boat_compasses(1)` while entering,
shown, or finishing the map's 250ms exit. On return 1 use
`wwhd_hud_boat_compasses_rect(&rect)` for the current normal rectangle. Its
`size=24`, `version=1`, x/y/w/h are TV HUD units, top-left origin, +y down; use
`WWHD_HUD_CENTER` anchoring. When return is 0 or negative, retain the existing
above-compasses placement. On normal exit completion call `(0)` once; the host
holds suppression another 200ms. Renewing during that delay cancels release.
Keep the minimap's existing tween fraction on reversals. Recheck the rectangle
getter during drawing to skip replacement contents if ownership was revoked after
the logic request. A missed renewal,
HUD unregister, lost eligibility or lifecycle invalidation revokes immediately;
there is no delayed stale HUD during a menu, disembark or load.

Use `wwhd_map_chart_selected()` in logic to follow GamePad choice. Call
`wwhd_map_chart_select(native_index)` on a minimap chart edge, rather than every
step. A result of 1 means queued; wait for `selected()` to report commitment.
`wwhd_map_chart_cancel()` cancels only this mod's uncommitted request. After
commit, unload/cancel restores no previous selection. Subsequent GamePad input
is ordinary player input. Draw callbacks must only use cached chart selection;
all selection/lease mutation calls reject the draw phase.

See [mod-sdk-v2.md](mod-sdk-v2.md) for API contracts and examples. Runtime test
results, retained files and limitations are recorded separately in the task report.
