# Right-to-left text (Arabic, Hebrew)

Fan translations into right-to-left languages work as plain content mods: install the
translation's language pack (`permanent_2d_*.pack`, and its title logo if it has one) with the
mod manager (docs/mod-manager.md, "Fan translations"), enable it and restart. A Cemu code patch
that ships with such a translation is not needed and not used: the port shapes and orders the
text itself.

## When it is on

At start the game opens its 2D language pack (the installed game's, a language source's or a
content mod's). If that pack's message font (`CKingMsg.bffnt`) has Arabic or Hebrew letters
(at least 20 of U+0621..U+064A or U+05D0..U+05EA), right-to-left text is on for the session and
the log says so:

```
[rtl] right-to-left text on: the message font has 36 Arabic and 0 Hebrew letters, 117 Arabic letter forms, 0 lam-alef ligatures, 0 harakat (missing ones are not drawn)
```

`WWHD_RTL=0` keeps it off, `WWHD_RTL=1` turns it on for any pack. For every other language the
hooks pass straight through to the game's code, and even with an Arabic pack a text without
right-to-left letters (a name, a number) is printed by the unchanged code.

## What it does

* **Shaping.** Arabic letters are drawn in their isolated, final, initial or medial form
  (Unicode joining types, Arabic Presentation Forms-A/B), lam + alef as the lam-alef ligature.
  Only forms the font has are used: a font without the ligatures (U+FEF5..U+FEFC) gets lam and
  alef as two joined glyphs, a missing form falls back to a related one or the base letter.
  Harakat (U+064B..U+0652) and other combining marks the font has no glyph for are left out
  instead of showing as boxes; they don't break the joining of the letters around them.
* **Order.** Each line is shown right to left (Unicode Bidirectional Algorithm without explicit
  embeddings: weak and neutral types, implicit levels, trailing spaces, reordering per line,
  mirrored brackets). Numbers and Latin words (a Latin player name, "Link") keep their left to
  right order inside the line; punctuation and button icons go with the words around them. The
  paragraph direction is right to left for every text that has a right-to-left letter.
* **Alignment.** Lines that the layout aligns left are aligned right: in a left-positioned text
  box to the box's right edge (message windows, item descriptions), otherwise to the right
  edge of the longest line. Centred and right-aligned text stays as it is.
* **Line breaks.** Most text boxes wrap lines that are too long for them (their width is the
  writer's width limit). The game breaks such a line after the last character that fits; with a
  right-to-left pack every text (Latin text too, so that measuring and printing agree) breaks
  after the last space instead. A line without a space is not broken.

## How it works

`tools/recomp/hooks_rtl.txt` hooks NintendoWare's text writer in the USA build, and
`runtime/src/rtl_text_hooks.cpp` (comments there have the details) does the following:

1. `TextWriterBase<wchar_t>::PrintImpl` (0x028709E0) makes a plan of the text it is given
   (`runtime/src/rtl_text.{h,cpp}`: per code unit the shaped or mirrored code to draw, which
   characters are not drawn, bidi levels). The game's tags (0x0E group type size params...;
   0x0F end tags) stay in place, so colours and sizes still apply to the right characters;
   button icons (tag group 3) count as neutral characters.
2. `CharStrmReader::ReadNextCharUTF16` (0x02871328) hands the writer the planned codes, so its
   own measuring (line widths, alignment, wrapping) uses the shaped glyphs.
3. The writer lays the line out left to right as usual. `CharWriter::PrintGlyph` (0x0286E8B4)
   records each glyph's pen position, advance and quad in the text box's glyph list;
   line breaks and the tag being processed come from two instruction hooks in PrintImpl.
4. After PrintImpl the glyphs of each line are put in visual order over the same span, aligned,
   and their quads' x positions moved. A text box rebuilds its glyph list only when its text
   changes, so this costs nothing per frame.

`lyt::TextBox::DrawSelf` (0x028785C8) supplies the box width for the right alignment. The
automatic line breaks are moved to spaces by instruction hooks in PrintImpl (0x02870FC8) and in
`TextWriterBase::CalcLineRectImpl` (0x0286FFCC, the measuring of one line; 0x02870028,
0x028704CC).
`runtime/tools/rtl_text_test.cpp` (CTest `rtl_text`) tests the shaping, bidi order and line
layout on their own.

## Limits

* The writer's tag processor is unchanged: a tag that draws something other than a button icon
  in the middle of a line (the Japanese ruby text) is placed with its line but not reordered
  inside it.
* Characters printed outside a text (measured by the game for its own layout) are shaped from
  their neighbours in memory; widths can differ slightly from the drawn text where a word
  continues across a tag.
* Layouts are not mirrored: a menu keeps its buttons where the English layout has them (Back on
  the left, Start on the right); only the text inside each box is right to left.
* Lines that the layout centres are centred as before; with a line broken automatically the
  measuring and the printing break at the same spaces, so they stay centred.
* Lam-alef: fonts without the ligatures (U+FEF5..U+FEFC), like the Arabic translation's, show lam
  and alef as two joined glyphs. A translation that wants the ligature adds those code points to
  its fonts; the port uses them automatically.
* Hebrew needs no shaping; it is untested with a real translation.
