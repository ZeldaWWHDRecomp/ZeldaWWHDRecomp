# Language sources (experimental)

A language source is for a port built from the **USA** game (title 00050000-10143500, version 0),
which has English, French and Spanish. It adds the languages of a European or Japanese copy of the
game that you also own: German, Italian, British English, European French and Spanish, or Japanese.
The game is still built from and runs the USA code; only the text, the fonts and the localised 2D
layouts are taken from the second game.

> **Built from the European game instead?** Then you need none of this: that build has English,
> French, German, Italian and Spanish itself and picks one by the console language, so choose it in
> the settings (`F1`) > Language and restart. Setup refuses a language source there. See
> [builds.md](builds.md).

> **Status: tested with a real European dump** (German, Italian, French, Spanish, English: title,
> file select, dialogues, GamePad menus, options, save prompt, name entry, the Pictograph Box; see
> [Results with the European game](#results-with-the-european-game)). **Japanese is untested.**

## Use

You need the USA game (installed as usual) **and** your own dump of the European
(00050000-10143600) or Japanese (00050000-10143400) game: a `.wux`/`.wud` image with its disc
key, a Cemu `.wua` archive or an extracted folder.

```
setup.py --language-source "Wind Waker HD (EUR).wux" --language-disc-key eur.key --common-key common.key
setup.py --language-source wwhd-eur.wua
setup.py --language-source /path/to/extracted/eur-game
setup.py --remove-language-source EU
```

(The interactive setup also offers it when the game is installed; the graphical installer's
protocol has `add_language_source`, see `tools/installer/README.md`.)

Setup checks the title id (the USA game, updates, DLC and other games are refused) and takes only
`content/Common/Pack/permanent_2d_*.pack` (about 12 MB per language) and `meta/meta.xml` into
`data/game-lang/EU` or `data/game-lang/JP`, with a `language-source.json` listing the packs and
their SHA-256. Nothing else of the second game is extracted or used, and nothing goes into the
repository or the release.

In the game, open the settings (F1) > Language: the source's languages appear under "From your
European game" / "From your Japanese game". The choice applies on the next start. Without the
overlay: `WWHD_LANGUAGE=3 WWHD_LANGUAGE_REGION=eu` (German from the European source),
`WWHD_LANGUAGE=0 WWHD_LANGUAGE_REGION=jp` (Japanese). `WWHD_LANG_DIR` points the game at another
folder of language sources (default: `game-lang` next to the game folder, else in the current
folder).

## How it works

### How the game picks its language (USA code)

* At boot (`SystemTask::prepare`, 0x0203FD18) the system setting object (singleton 0x101F4BAC)
  reads the console language with `UCReadSysConfig("cafe.language")` (0x025F9448). It keeps
  `+0x10` = region in Wii U region bits (1 Japan, 2 USA, 4 Europe) and `+0x14` = console language
  (0 ja, 1 en, 2 fr, 3 de, 4 it, 5 es, ...). **The USA build always stores region 2 and keeps only
  languages 1, 2 and 5** (anything else becomes English); it also mirrors the language into the
  save's options (byte +4 at save + 0x12F0: 0 English, 2 French, 3 Spanish — the GameCube PAL
  order 0 en, 1 de, 2 fr, 3 es, 4 it; the USA code only copies it to 0x101EA6D2, never reads it).
* Every other user of the setting has the European and Japanese branches compiled in:
  * the 2D pack, 0x02612BE0: region 1 → `Pack/permanent_2d_JpJapanese.pack`; region 2 →
    `UsFrench`/`UsSpanish`/`UsEnglish`; region 4 → `Eu<English|French|German|Italian|Spanish>` for
    languages 1..5 (table 0x100E2408). All nine names are in the USA executable (0x1048DD4C).
  * the pack holds everything language-dependent of the 2D side: all MSBT message archives
    (`message*_msbt.szs`, one `*_msbt.szs` per layout, `unitString`, `rubyString`), the MSBT project
    `CKing_msbp.szs`, the six fonts (`CKingMain`, `CKingMainL`, `CKingMsg`, `CKingPic`, `CKingRuby`,
    `CKingZelda`) and all in-game layouts (221 files). In the three USA packs only the 58 MSBT
    archives differ; layouts and fonts are byte-identical.
  * the message manager (0x025F4A60, 0x025F416C) loads the project and its message sets from the
    loaded pack by name (fallback path `Cafe/US/Message/Us<Language>/<name>.szs`, unused);
    messages are looked up by label.
  * layout panes with a language suffix (0x0270517C region index 0 JP / 1 US / 2 EU, 0x027051B0
    language index, suffix table `_JpJa _UsEn _UsFr _UsPo _UsSp _EuEn _EuDu _EuGe _EuFr _EuPo
    _EuRu _EuSp _EuIt`): even the USA layouts contain `_JpJa` panes (message windows, menus, the
    treasure map; `N_TitleLogo_00_JpJa` in `Common/Layout/Title_00.szs`).
  * the software keyboard for the name (0x026195C4): region 1 → Japanese keyboard, region 4 →
    per-language European layouts (QWERTZ for German...).
  * the error viewer's region and language (0x02033118, 0x0203315C), and the time format of
    messages (0x025FE38C: English and German put the minutes first).
* Region-independent: the audio (`Cafe/US/AudioRes/JAudioRes`, fixed path in the USA code), the
  3D packs (`szs_permanent*`, `permanent_3d`, `first_szs_permanent`), stages, objects and
  `Common/Layout` (which already has the Japanese title logo pane).

### What the port does

* **Setup** takes the packs of the second game (`wwhd-extract --only`).
* **Runtime** (`runtime/src/game_languages.{h,cpp}`) finds the packs in
  `data/game-lang/*/content/Common/Pack` (names without case, `SARC` header, packs the USA game
  has itself are skipped) and offers them in the Language tab.
* **Console language** (`hle/coreinit_misc.cpp`): with a source language chosen, the game gets
  that language code (3 for German, ...).
* **Region** (`language_region.cpp`, hook on 0x025F9448 in `tools/recomp/hooks_language.txt`):
  after the USA reader, the source's region (4 or 1) and language are written to the setting object
  and the options byte as the European game would keep them (the European reader, 0x025F9738, stores
  region 4, keeps languages 1..5 and maps them through its table 0x100E0D7E: 0 0 2 1 4 3, the same
  as `options_language()`). The game then asks for `permanent_2d_EuGerman.pack`, through its
  "local" file device, as `/vol/content/Cafe/JP/Pack/permanent_2d_EuGerman.pack` (a folder no disc
  has; the redirect and the content mods match the pack under any `Pack/` folder).
* **File system** (`hle/fs.cpp`): a read of the active source's pack name (any case, any prefix) is
  served from the language source; a content mod's file of the same name still comes first;
  writers are never redirected. Nothing else is redirected.
* The name prompt reads its allowed characters from the active pack's `CKingMsg` font
  (`overlay/game_font.cpp`).

## Results with the European game

Tested with the European disc (00050000-10143600, v0; `setup.py --language-source` with the `.wux`
and its key; 57 MB taken, five packs):

* **Files**: the European disc differs from the USA disc only in the five 2D packs, the audio folder
  (`Cafe/EU/AudioRes`: the same 70 file names and sizes as `Cafe/US/AudioRes`), `code/` and `meta/`.
  All 937 other content files (`Common/Layout`, `Jpeg`, `ProgramTexture`, stages, objects...) are
  byte-identical, so nothing else needs to be taken.
* **Packs**: the same 221 members as the USA packs; the 2D layouts and all six fonts are
  byte-identical to the USA ones (the USA fonts already have every character the European text
  uses), only the MSBT message archives differ. Every message label of the USA packs exists in the
  European ones and the other way round, and the European text uses no control tag the USA text
  doesn't (159 kinds; the USA French/Spanish use 9 more).
* **In game** (German, Italian, French, Spanish, English): title screen, file select ("Präludien",
  "Isola Primula", European date format), dialogues with umlauts, ß, accents and coloured words,
  wrapping in the European text boxes, GamePad item menu, options with their long descriptions, the
  save prompt, the Pictograph Box prompt, the opening text, name entry with "Größe", "Niccolò",
  "Żółć". No missing glyphs, no overflow, no allocation problems in the logs. The name keyboard is
  asked for in the chosen language (swkbd language 3 for German).
* **European-only code** (a comparison of the two executables: 18 functions differ). Ported:
  * the German genitive of the player's name in messages 0xC8B, 0x1D21 and 0x31D7 ("Links Oase",
    "Lukas' Oase"): the European code appends "s", or "'" after s/x/z, when the language is
    German (0x025F85AC for the name string, 0x025FC700 putPlayerName). `language_region.cpp` does
    the same at 0x025F8720 and 0x025FC668.
  Not ported (open, need a comparison with the European game running on a console or in Cemu):
  * four message-window branches that the European code takes only for English and the USA code
    always takes: a pane at +0x12C shown except for messages 0x1076..0x1078 (USA 0x026AEDF4), the
    text pane `T_Msg_00` at +0x130 shown (USA 0x026B6C4C; seen at the start of a shop talk), panes
    at +0x140/+0x144 toggled after a state change (USA 0x026B40D0, 0x026B40EC), and a cursor x
    position from the item's own width instead of a per-index table (USA 0x026FE3D0). In the
    European game other languages hide those panes or use the table. Nothing visibly wrong was seen
    in the tested scenes.
  * the message router (European 0x025F795C) sends messages 0x266 (the Hero's Charm) and
    0xEDC..0xEE3 (the Pictograph Box prompts) to dedicated windows; the USA code uses the default
    window. The German Pictograph prompt fits.
  * d_meter (HUD) and a Gohdan camera function differ in code size; no language checks found there.

## Known risks

* **The European/Japanese builds differ in code**, not only in the region constant (see above for
  the European game; the Japanese one is not compared yet). The USA
  executable contains the European and Japanese branches listed above (and the build path
  `ProductUS` suggests one code base per region), but a region-specific difference elsewhere (for
  example in the message tag handling or the text layout) would not be seen until the real files
  are played.
* **Message labels**: the USA code asks for messages by label. A label that exists only in the USA
  text (or a European-only label the USA code never asks for) would show empty text or fail a
  lookup.
* **Memory**: the pack index and its files are loaded into heaps sized by the USA build. Longer
  German or French text, or a larger Japanese font, could exceed a heap.
* **Text that does not fit**: the European layouts come from the European pack, so boxes sized
  for German should come with it; code-side widths (USA) could still clip.
* **Japanese**: the Japanese pack uses ruby (furigana, `CKingRuby`, `rubyString`) and the `_JpJa`
  panes; the USA code has both, but only the Japanese build may enable everything (for example a
  ruby option). The JP keyboard and name characters depend on the font (`CKingMsg`).
* **Save files** are shared by all languages; the name is stored as text, so a Japanese name shows
  in other languages only where the fonts have those characters (the USA `CKingMsg` has kana and
  1,235 kanji, the menu font `CKingMain` kana and 75 kanji).
* **Save states** are tied to the language they were made in (the loaded text is in the state).
* Region-specific files outside the packs (the EU disc's `Common/Layout`, `Jpeg`,
  `ProgramTexture`, `Cafe/EU/AudioRes`) are not taken; if they differ, the USA ones are shown.

## Test plan for a real dump

0. **Before any European dump** (checks the region path with the USA files only): make a folder
   `X/EU/content/Common/Pack` with a **symlink** `permanent_2d_EuFrench.pack` → your USA
   `permanent_2d_UsFrench.pack`, start with `WWHD_LANG_DIR=X WWHD_LANGUAGE=2
   WWHD_LANGUAGE_REGION=eu`. The log must say "language source active ... region 4"; the game
   must ask for `permanent_2d_EuFrench.pack` and show French; the name keyboard is AZERTY.
1. `wwhd-extract list` on the European/Japanese image: compare the file list and sizes with the
   USA game. Every file outside `content/Common/Pack/permanent_2d_*` that differs is a candidate
   for the language source (note especially `Common/Layout`, `Jpeg`, `ProgramTexture`,
   `Cafe/EU`).
2. `setup.py --language-source ...` with the image, the `.wua` and a folder; check
   `data/game-lang/EU/language-source.json` (five packs), and that nothing else was extracted.
3. Per language (German, Italian, British English, French, Spanish; Japanese): boot, title screen
   (logo, "Press Start"), file select, new game, name entry (keyboard layout, umlauts, ß, accents,
   kana/kanji; the name in dialogs and on the file select), the opening (Grandma, Aryll, the
   telescope), a full dialogue scene, shops (numbers, rupees, plurals), the auction (timers), the
   Pictograph and the Pictobox text, the sea chart and treasure maps, the item menu (long item
   names), the quest status, the options menu, the game over and save screens, the boat race and
   other timers (`putTimeF`), the Tingle Bottle and Miiverse texts, the credits.
4. Long German strings: item descriptions, the Hero's Charm and Tingle Bottle texts, the options
   (Kamerasteuerung...), the save messages: nothing clipped or overflowing the box; text speed and
   line breaks as on the European console.
5. Japanese: kana and kanji in dialogues, ruby (furigana) above kanji, the title logo
   (`N_TitleLogo_00_JpJa`), `_JpJa` panes in the message windows, the treasure map and the item
   menu, the Japanese name keyboard.
6. Fonts: no missing glyph boxes (European accents in the menu font `CKingMain`, the GamePad
   screen).
7. Saves: make a save in German, load it in English and Japanese and back; the name and the
   progress must survive; a USA save loads in every language.
8. Memory: play several scene changes and the menus in German and Japanese with the heap
   statistics; no allocation failures in the log.
9. Compare against the European console or Cemu running the European game for the same scenes
   (screenshots), to separate port problems from the original's.
