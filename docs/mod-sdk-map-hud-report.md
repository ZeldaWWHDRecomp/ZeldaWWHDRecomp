# Map/HUD SDK implementation report

Local branch `codex-sdk-map-hud`, based on GitHub `devel` at `acffa936`.
Runtime implementation: `193469f4`; older-host helper regression: `b73b6705`.
No push, PR, user-save access, or deletion was performed.

Both requested SDK additions are implemented. The minimap itself is unchanged;
its integration belongs in the lead's follow-up mods PR. The exact calls,
capability checks and undo rules are in [the integration note](mod-sdk-map-hud.md)
and [SDK reference](mod-sdk-v2.md#native-boat-compass-lease).

## Design decisions

- The compass lease suppresses the exact native compass layout at the existing
  pane draw boundary. Native updates and input continue. It never calls `toWait`,
  reparents panes, changes native transforms, or restores saved HUD state.
- `wwhd_hud_boat_compasses_rect` is an additional read-only getter because the
  proposed lease's integer result cannot also carry geometry. It reports the
  visible, screen-clipped bounding rectangle of native pictures, including their
  current animation transforms. Picture bounds are intersected with the TV; native
  alpha-inheritance rules are honored. Containers and rotating needles alone are not
  used as the rectangle. The runtime fixture's root was `L_CompassClock_00`.
- The caller renews during its own 250ms map exit, then releases once; the host
  holds suppression for another 200ms. A reversal renews immediately. Missed
  renewals and invalidation revoke immediately. The caller retains its map tween
  fraction; the native compass animation continues underneath suppression.
- Chart selection is an explicit-index host event at the native carousel event
  boundary. It makes the same outgoing/selected/direction transition as native
  previous/next events, then delegates art/caption/processing to the native
  carousel. No global input-queue synthesis, repeated carousel stepping, child
  artwork setter or screen initialization is used.
- Default C helpers use the SDK's existing optional query transport, so even
  older installers can translate a mod that retains the older-port fallback.
  Direct imports are also available; translator 0.3 makes those optional on an
  older runtime. The module ABI remains 1; the new feature version is 1.
- USA/EU identities are mapped and checked before installation; missing entries,
  unsupported builds or conflicting replacements leave capabilities zero.
  [Native evidence](mod-sdk-map-hud.md#native-identities-and-integration) records
  canonical addresses, offsets and the native lifecycle.

## Validation

| Check | Result and limits |
| --- | --- |
| Current-source Mac port build, `-j 8` | Pass. Game code regenerated from the installed USA v0 RPX with current hooks and `--mod-hooks`. An initial attempt with an older generated-code directory failed to link new devel hooks; that directory was retained and the current generation fixed the link. |
| Focused CTest | Seven pass: `map_hud`, `map_hud_native`, `guest_hooks`, `guest_mods_no_hooks`, `guest_mods_editor`, `guest_live`, `guest_settings`. |
| Guest translator tests | 20 pass with PowerPC clang/lld. Direct-import wrappers execute against missing and present services. Default helpers execute against a host providing only the old query service and return unsupported; their only service import is `wwhd_setting_get`. |
| Strict native adapter compile | Pass with `-Wall -Wextra -Werror`. |
| UndefinedBehaviorSanitizer | Pure lease/queue tests pass. No ASan claim. |
| Regional translation | USA test guest loads/runs. EU test guest translation/native-module compilation passes; EU gameplay was not run. |
| Boat compass replacement/restoration | Verified visually on Mac, USA, Metal, using the default query helpers. A purple test rectangle replaces the compass area; native compasses return after release; renewal replaces them again. |
| SDK chart selection on GamePad | Verified visually and in logs: native index 4 shows Treasure Chart 2, index 3 shows Treasure Chart 1. Native carousel processing finishes normally. |
| Later player selection | The test mod stops selection requests; a normal scripted GamePad chart-arrow touch changes native index 4 to 5, showing Treasure Chart 3. No SDK commit to index 5 occurs. |

The real native adapter is tested on synthetic guest objects, not only a mirrored
state machine. Tests cover overlapping owners, missed renewal, explicit release,
reversal around 250ms/450ms, hidden-to-hidden/no acquisition, disable/revoke,
state loading, first person/event gates, HUD/layout/stage replacement, visible
rectangle clipping, chart ownership/opening/deciphering rejection, draw-phase
rejection, cancellation before commit, replacement during animation, and later
native input. Native selection is unchanged by cancellation and state-load
invalidation. Destruction and address reuse invalidate host ownership.

No older shipped port binary was launched; compatibility is checked with the
old-service-only driver and the import surface.

These lifecycle cases are host/synthetic-memory verification, not claims that
all were replayed in game. Actual unload, arbitrary cutscenes/first-person/cannon/
grappling-hook combinations, day/night changes, and frame-accurate 200ms return
measurement were not separately captured. Their native update/control paths are
unchanged; only drawing is suppressed. The test guest does not implement a full
250ms minimap exit animation; that timing remains the caller's responsibility.

A failed HUD callback/list or HUD unregister also revokes ownership. The final
review moved unregister revocation before the HUD registration mutex to preserve
lock order; that small lock-order change has regression-build/startup checks,
while the visible fixture predates it. All visible behavior uses the same bridge
and wire protocol as the final source.

## Scripted scene and evidence

Private evidence root:
`/Users/lschaupp/projects/zelda_wwhd-work/sdk-map-hud-proof/`.
Each run retains environment, isolated save/state, package, logs and screenshots.
All runs were muted, with one owned game process at a time. The launcher checks
load immediately and refuses above 90; heavy port builds were started under that limit too.
Several attempts were held by the guard. No heavy run bypassed it.

The portable fixture derives exclusively from the blank Quest Log named “Parity”
created during the minimap task. It authors native boat ownership/riding restart
metadata, chart ownership/opening/deciphering and consistent pearl/sword progress.
Completed dialogue bits were identified in `daShip_c::checkForceMessage`
(`0x024731BC`); the Sea Chart collection bit was identified in `hasMenuMap`
(`0x0268D360`). Native portable restart creates the boat and riding procedure.
No fixture overrides live HUD fields, native carousel state, or UI event queues.
The MAP tab and subsequent chart arrow are operated by ordinary scripted input.
This is an authored test scene, not a normal progression/playthrough claim.

| Evidence | Paths under the private root |
| --- | --- |
| Native compass area replaced | `sea-07/frame_9000_present.png` |
| Native compasses restored | `sea-07/frame_9200_present.png` |
| Replaced again; GamePad Treasure Chart 2 | `sea-07/frame_9500_present.png`, `frame_9500_drc.png` |
| Later GamePad Treasure Chart 3 | `sea-07/frame_11100_drc.png`; `log-retry` reports selected index 5 after SDK requests stop |
| Native suppression and event lifecycle | `sea-07/log-retry`: exact layout-root suppression, carousel commits at idle phase 0, native 4 → 5 without another SDK commit |
| Guest source and package | `test_mod.c`, `sea-07/manager/Mods/sdk-map-hud-test/` |
| Build/test diagnostics | `build-review.log`, `ctest-final.log`, `translator-tests-20-final.log`; earlier failed/held diagnostics are retained too |

Earlier fixtures are retained: `sea-01` was blocked by native story dialogue;
`sea-02/03` exposed geometry/draw-boundary issues and an unavailable MAP tab;
`sea-04/05` verify chart selection and later player input; `sea-06` establishes
native pane ancestry/alpha behavior. They are not compass-replacement passes.
Screenshots are private game images, not repository assets or a published video.

## Retained files

Nothing was deleted. Exact paths, byte sizes and file counts are in private
`retained-paths.json` (refreshed at completion). Main retained directories:

| Path | Approximate size |
| --- | ---: |
| `../sdk-map-hud-proof/sea-01` | 119 MiB |
| `../sdk-map-hud-proof/sea-02` | 125 MiB |
| `../sdk-map-hud-proof/sea-03` | 112 MiB |
| `../sdk-map-hud-proof/sea-04` | 136 MiB |
| `../sdk-map-hud-proof/sea-05` | 119 MiB |
| `../sdk-map-hud-proof/sea-06` | 100 MiB |
| `../sdk-map-hud-proof/sea-07` | 119 MiB |
| `build/` in this worktree | 511 MiB, including current generated game code and objects from the initial generation attempt |

The proof root also retains guest modules, test binaries, source, logs and
checksums. The earlier standalone `../map-hud-test` binary is listed in the
inventory. The source checkout is the deliverable, not a cleanup candidate.
`README.md`, `CHANGELOG.md` and shared checkout source were not edited; the board
has append-only progress notes and is not committed.
