# The Settings page in the item menu

The common settings have a page of their own in the game's item menu, drawn like the game's
menus. Its letters, paper and buttons are read from your game files while the game runs, and its
sounds are the menu sounds of your game. The full Settings window (Esc, F1, or hold Select on a
controller) stays for every other option.

The idea comes from the GreenNaugahyde Android fork, whose options page slides in left of the
item page. This port has its own implementation, which works the same on the desktop, the Steam Deck
and Android.

## Opening it

Press **+** to open the item menu. On the items page:

- **Controller or keyboard:** move the menu's cursor to the left column (Items, Save, Options) and
  press **Left**, or press **ZL**.
- **Touch or mouse:** tap or click the page arrow at the left edge, or drag it to the right. The
  page follows your finger. Let go past a quarter of the screen and it opens; otherwise it slides
  back.
- **Pro Controller:** the item menu is on the TV, so the mouse works on the TV picture.

The page is only offered on the items page, not on the second page or while a bag is open.

## Using it

| | Controller / keyboard | Touch / mouse |
|---|---|---|
| Tabs | L / R | Tap a tab |
| Choose a setting | Up / Down (or the stick) | Tap its row |
| Change it | Left / Right, or A | Tap its value. On numbers and choices, the left half steps down and the right half steps up |
| Back to the items | B or + | The arrow at the right edge, or **Back** |

The buttons follow your **Face buttons** setting, and the button pictures follow **Button
prompts**. Changes apply at once and are saved, the same as in the Settings window. Both show the
same values.

## What is on it

- **Camera:** invert up/down, direct camera and its speed, mouse camera and its sensitivity,
  first person on R3 or the mouse wheel, letterbox while targeting.
- **Controls:** face buttons, button prompts, shoulder names, coloured face buttons and their text,
  rumble, stick dead zone, skip controller selection.
- **Screen:** gameplay field of view, the boost bar. On Android also the touch buttons and the
  on-screen controls.
- **Graphics:** internal resolution, frame rate (30, 40, 60, 120 or 240 fps), dynamic
  resolution, bloom, edge smoothing (FXAA), anisotropic filtering, clouds, cloud shadows and
  full-size torchlight. They apply at once. The rest (renderer, aspect ratio, HDR, ambient occlusion
  and more) is in the Settings window's Graphics tab.
- **Game:** *Change Link's name* gives the save file being played a new name of up to 16 characters.
  The game itself keeps 8: the file select shows the first 8, and so does a copy of the save in the
  original game. The whole name is kept with the save (in the port's part of `cking.sav`, which
  backups, the save folder and save exports carry along) and the game's texts use it. (Idea from
  the GreenNaugahyde Android fork.)
- **Game:** the built-in gameplay mods that apply while the game runs: quick doors, fast scene
  changes, fast forward, instant text, no pauses in texts, jump anywhere, Deku Leaf: wave to rise, climbing, run/swim speed, enemy lock-on, quick-use items,
  ladders, sidling, ivy, ledges, sailing speed, automatic wind. Their finer options (speeds, buttons)
  are in the Settings window's Mods tab.
  - *Quick doors*: Off, Faster or Walk through. Faster runs every door scene four times as fast.
    Walk through: dungeon shutters and house doors open as Link walks up, he walks through without the
    door scene and the door shuts behind him; leaving a house is quicker too. Locked doors, boss doors
    and doors with bars keep their scene, sped up as with Faster. *Walk-through doors open with A*
    makes those doors wait for A on their Open prompt instead. (From the GreenNaugahyde Android fork.)
  - *Instant text*: message boxes show their whole text at once instead of letter by letter; each box
    still waits for A. *No pauses in texts*: the short pauses written into some texts are left out.
    (Ideas from the GreenNaugahyde Android fork.)
  - *Deku Leaf: wave to rise*: while Link glides on the Deku Leaf, each wave of the leaf (its button
    again) lifts him a little; holding the button waves on. The height of one wave (50 %, 100 %,
    150 % or 200 %) is in the Mods tab. The glide still uses magic. (Idea from the GreenNaugahyde
    Android fork.)
- **Mods:** options of your enabled mods that their authors put on the page. An option that only
  applies at the next start says so.

The page uses the game's language for its own texts in English, French, German, Italian and
Spanish. Other languages show English. Mod options show their mods' own texts.

## For mod authors

To put an option on the page, add `"menu": true` to it in your manifest. See
[mod-sdk-v2.md](mod-sdk-v2.md#package-and-install) for the optional `"section"` and `"live"` fields. Mods
can read the page's port settings with `wwhd_setting_get("settings.<id>")`.
