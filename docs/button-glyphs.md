# In-game face-button glyphs

The **Face buttons** setting introduced by PR #97 already gives Xbox buttons the right
semantics: in *by label (Xbox)*, Xbox A drives game A (accept/interact), B drives game B
(back/sword), and X/Y drive the corresponding item actions. A literal A/B or X/Y letter
swap would therefore tell the player to press the wrong printed button.

In *by label*, the dialog icons and HUD face-button backgrounds now use an Xbox-style
colour treatment: **green A, red B, blue X, yellow Y**. *By position (Nintendo)* and
*custom* use the game's authored colours. The letters continue to identify game actions.
The HUD's four-face cluster also follows the Xbox positions: **A bottom, B right, X left,
Y top**. Switching back restores the Nintendo arrangement. Whole button groups move,
including their letters, assigned-item art, command text and the special/parry A effect.
In this mode shoulder prompts use **R1 / R2** for R / ZR, and **L1 / L2** for L / ZL
where they appear (including ZL-targeting instructions).
This uses the player's existing game art; no Nintendo font, texture or modified pack is
distributed, and no new controller preset or controller-brand detection is involved.

## Investigation and implementation

The repository had neither `.graphify/graph.json` nor the legacy `graphify-out/graph.json`.
The attempted orientation query reported the missing graph.

The game's `CKing.msbp` names group 3 `PictureFontTags`; tag types 0, 1, 5 and 6 are
A, B, X and Y. The game's lookup at canonical USA address `0x025F90C8` maps these to
`CKingPic` characters U+E000, U+E001, U+E002 and U+E003. Its three callers handle drawing
and measuring. Remapping those tag types changes letters, rather than the artwork's
convention, and was rejected for this follow-up.

The 2D pack's `CommandGuide_00.szs`, `CommandA_00.szs` and `IconSlideArea_00.szs` share
the **same** `ABXY^f.bflim` background; their A/B/X/Y letters are separate text panes.
Replacing that shared texture alone cannot give the four buttons different colours.
`mods::content` activates replacements before guest execution and explicitly requires a
restart. The Cemu adapter supports surface-size and shader rules, not PNG/DDS/BFLIM image
replacement. Draw-time styling is smaller and supports live setting changes on both
renderers without adding texture-reload infrastructure.

`tools/recomp/hooks_glyphs.txt` hooks three functions:

* `0x0286E9F0`, `CharWriter::Print`: record face identity in unused bits of a newly built
  glyph quad's byte +0x2F. The game resets that byte on each build (the emitter stores zero,
  then sets bit 0 only for a colour font), and every reader masks out only its own bits (see
  below). Nothing is written while by label has never been on in the session, so players who
  never use it run exactly the stock path. The first time it is selected, each cached text box
  is rebuilt once so its glyphs are marked and the draw hook can restyle it; after that,
  marking happens in every layout, so even text cached before a layout change is identifiable.
  The metadata follows guest save states and heap reuse naturally.
* `0x028F7C7C`: colour those quads while the game builds GPU vertices, then restore the authored
  colours. Bit 3 of each marked quad records the style used for the generated vertices. The
  existing `0x028785C8` TextBox draw hook checks this before the game's cache-validity gate
  (list byte +6), invalidating it only when a face icon's rendered style differs from the live
  choice. The original code then rebuilds the text and vertices. A same-style frame reuses the
  clean cache. This works with the RTL hooks and leaves the outline pass to the game. RGB
  multiplication preserves shading; alpha is unchanged.
* `0x02874D54`, `Picture::DrawSelf`: apply the same palette to the named face backgrounds
  `P_A_00`, `P_B_00`, `P_X_00`, `P_Y_00` and `P_ASpecial_00`, restoring their four authored
  vertex colours afterwards. This covers the HUD, floating A prompt and item-assignment
  backgrounds without altering their separate letter text.

The live layout is cached per guest thread using `input_map::generation()`. Only
`kLabels` enables styling, including when *Automatic* resolves a pad to it: `face_layout()`
reports the shape the preset resolved to, and the generation bumps whenever the bindings are
rewritten, so the hooks follow the pad that is playing. The hooks use the common game/GX2 path,
not SDL, AppKit or a particular renderer. `input_map.cpp` has no changes. The EU build map maps
all three function entries and reports their bodies unchanged.

### The glyph quad's word at +0x2C

Each cached glyph quad is 0x30 bytes. Its last word (`+0x2C`) carries three subfields; in the
generated `build/gen` code these are the only places that touch it:

* `0x0286E4FC` — the emitter reached from `CharWriter::Print` through `0x0286E8B4` — writes the
  signed 16-bit value at `+0x2C` (`0x0286E838`), copies the source glyph's page byte to `+0x2E`
  (`0x0286E840`), stores zero to `+0x2F` (`0x0286E84C`), and sets bit 0 afterwards only when a
  virtual call reports a colour font (`0x0286E870`, `0x0286E878`). It never inspects or
  preserves the high bits, so a freshly built quad starts with `+0x2F` at `0x00` or `0x01`.
* `0x028F7A4C` — batch grouping, called from the vertex builder — loads `+0x2F` and keeps only
  bit 0 (`0x028F7B00`, mask `& 1` at `0x028F7B04`).
* `0x028F7C7C` — vertex generation — reads the 16-bit value at `+0x2C` (`0x028F7E54`,
  `0x028F8080`) and the page byte at `+0x2E` (eight sites from `0x028F7E70`). It never loads
  `+0x2F`.

No copy or clear function moves the word: the append path rewrites every field, and heap reuse
is covered because the emitter resets `+0x2F` on each build. The private marker bits (the D/E
prefix, the two identity bits and the recorded-style bit, bits 1–7) therefore live in a byte
whose only game reader masks bit 0 and whose only game writer wipes it before writing.

### HUD positions

The existing `0x028766CC` pane-matrix hook composes face placement with aspect anchoring.
It recognises the complete CommandGuide cluster under `N_All_00`: `W_SetSeatA_00`,
`P_B_00`, `N_X_00`, `N_Y_00`, plus the sibling `P_SetSeatASpecial_00` for A's parry effect.
These groups have differently nested button pictures. Their centres are computed from
authored local translations, scale and Z rotation in the common parent's coordinate system.
By label exchanges A/B and X/Y **positions**, not their letters or actions.

Temporary group-root offsets remain active while the game recursively calculates child
matrices, then are restored. Only the resulting global matrices retain the new arrangement;
animation/gameplay continue to own authored locals. The recognised groups force parent-dirty
propagation in either layout to support live switches and cached save-state restores without
accumulating offsets. The standalone floating A prompt and the item screen's linear row are
not four-face clusters and are not rearranged.

### Shoulder labels

The icon-font shoulder characters U+E083..E086 (L, R, ZL, ZR) are marked with prefix E
in the same cached-quad metadata. In by-label mode their texture/UV references temporarily
select L1/R1/L2/R2 captions composed at runtime from the player's **CKingMain** HUD font,
the same font as the HUD's A/B/X/Y letters. The four L/R/1/2 glyphs are queried through
the native Font::GetGlyph ABI, with their bearings, advances and antialiased coverage.
The font's BC4-compressed GX2 sheets are decoded using the existing address library and
CPU BC decoder; uncompressed alpha formats and packed-alpha sheets are also supported.
Native sheet Y is bottom-up and is converted to top-down before composition, without
mirroring X or reversing letter/digit order. No low-resolution bitmap alphabet is used.
The resulting single-layer RGBA atlas is kept in host-only storage, keyed by the font
and its resource identity. Font resources are treated as immutable within the session,
as are the existing restart-only content replacements. Unsupported resources cache a
failed result; not-yet-loaded fonts are retried when available. No Nintendo artwork is
distributed. Dialog glyph size and advance stay authored, so message measuring and line
breaks still agree with rendering.

The HUD's shoulder letters are ordinary text, not group-3 icons. The TextBox hook scopes
the exact `T_L_00`, `T_R_00`, `T_ZL_00`, `T_ZR_00` panes under their matching picture parents,
and validates that the source text is the expected Nintendo label. Vertex generation uses
the atlas's lettering-only column, centring it across the original label and fitting the
R1/L1 labels to the background. A temporary one-quad render replaces both characters of
ZR/ZL; the original text, quad count, texture references, geometry and shader flag are restored.
Authored HUD colour/fade remains in force. Switching modes invalidates the cached vertices
as it does for face icons. Cached batch references are also validated against this process's
atlas after a save-state restore. The atlas uses the existing game's font-sheet/GX2 binding
path. If the font is not ready or its format unsupported, the entire original label is
retained instead of truncating ZR/ZL to one letter. The original font-page index is restored
after rendering; replacement vertices always select the composed atlas's single layer.

## Checks

The focused hook test uses synthetic guest memory and stubs the original functions, including
the TextBox cache-validity gate. It checks cached icons across position → labels → position/custom
transitions; identity of the requested character; exact restoration of authored colours;
alpha/outline preservation; the full four-face HUD palette; unrelated icons/panes; and a
full glyph list that cannot append another quad. No game artwork is part of the test.
The session gate is covered too: before any activation the printers leave the game's flag bytes
untouched and a stock vertex pass writes nothing to the quads; selecting by label for the first
time rebuilds the cached dialog once, after which the same quads are marked and styled across all
four mode switches. The Automatic preset is exercised with an Xbox pad resolving to by label
(marking, vertex styling, HUD picture colours) and a Nintendo pad resolving to by position
(stock colours).
The positional regression reproduces the reported A-at-right error using CommandGuide's
nested hierarchy, then checks all four Xbox positions, associated item/parry children, live
mode switches with clean cached matrices, unchanged authored locals, viewport/container
transforms, restored stale globals, and non-HUD or incomplete clusters.
The positional test executes the actual aspect hook, with guest matrix computation stubbed,
including 21:9 TV anchoring and the same layout reported on the GamePad.
Shoulder tests additionally verify all four dialog labels and HUD text labels, numbered sprite
content, reversible texture/UV/shader substitution, unmodified source text/metrics, live
cache transitions, and stale atlas batch references restored from another host process.
An asymmetric coverage fixture checks both axes and fails if the vertical normalisation
is removed. The real game's BC4 font atlas was captured locally: all four complete captions
were visually inspected upright and in left-to-right order after the correction.
The fallback regression also checks that an unavailable/unsupported font preserves both
characters of ZR without repeated vertex rebuilds, and that a valid font appearing later
updates the existing cached label to R2.

Verified on Linux with Clang 22: `input_map_test` (590 passed, 0 failed), the production-hook
test (cached face/shoulder icons, HUD text and positional checks), all 59 CTest tests, and `wwhd` compiled
and linked first with `build/gen-stub` generated by
`python3 tools/recomp/stubgen.py build/gen-stub`. The playable code was subsequently regenerated
from the player's USA `cking.rpx`, compiled with Clang and launched successfully on Linux/Vulkan.
The positional correction and its nested-matrix regression also pass all 59 CTest tests, and
the playable executable was rebuilt with that correction. Synthetic checks establish the
hook behaviour, not actual GPU appearance. The 17 build-map tests also passed, and the EU map
reports all three new hooked bodies unchanged.

Before opening the PR, current `devel` was merged and the playable USA code regenerated
again. The complete Linux build and expanded suite passed: **78/78 CTest tests**, including
the glyph regression composed with the updated aspect anchoring.

After the review, current `devel` was merged once more and the USA code regenerated with the
merged recompiler. The complete Linux build and full suite passed: **151/151 CTest tests**
(`input_map_test`: 709 passed, 0 failed), including the gate and *Automatic* cases below.
(`mod_oneclick_flow` requires more than 10 GiB free in its temporary directory; it passes with
`TMPDIR` on the data volume.)
The generated USA code was also re-inspected for the glyph quad word after regeneration: the
same three functions and masks are the only accesses at `+0x2C`–`+0x2F` on the text path.

Regenerate and rebuild the real game code with the new hook list before play-testing.
The requester tested the final Linux/Vulkan playable build and confirmed the corrected
appearance after the HUD placement, native-font and orientation fixes.

The remaining targeted visual matrix includes tutorial/dialog prompts, the special/parry
A effect, item-assignment screen, fade/disabled animations, translated/RTL fonts, and
save-state restore. On macOS, verify both Metal and Vulkan plus the AppKit Controls
window/Input menu. Face colours retain the game's shapes; shoulder captions use the game's
native HUD lettering in a composed atlas. Additional replacement artwork remains available
through content mods.
