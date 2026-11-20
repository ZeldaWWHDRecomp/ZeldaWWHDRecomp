# SPDX-License-Identifier: MPL-2.0
"""Guard the shipped EUR map and region-aware runtime address uses.

This is a source audit, not a C++ parser or proof of matching field layouts.
Keep non-address exceptions narrow and documented; new game literals must use
GC/GD and have a shipped mapping. Canonical host dispatch identities use the
explicit CanonicalCodeId marker and still require a shipped code mapping.
"""
from pathlib import Path
import re
import unittest

import builds

ROOT = Path(__file__).resolve().parents[2]
NON_ADDRESS = {
    ("runtime.h", 0x02800000): "foreground bucket size",
    ("runtime.h", 0x02000000): "MEM1 size",
    ("core.cpp", 0x02000000): "executable memory lower bound",
    ("crash_memory.cpp", 0x02000000): "executable memory lower bound; no guest function address",
    ("gfx/metal_main.mm", 0x02000000): "executable memory lower bound",
    ("hle/system_stubs.cpp", 0x02000000): "executable memory lower bound",
    ("mods/mod_archive.cpp", 0x02014B50): "ZIP central directory signature",
    ("mods/lyt_build.cpp", 0x02020000): "the layout files' format version (BFLYT / BFLAN)",
    ("mods/item_ring.cpp", 0x10101CC0): "RGBA colour of the item ring's entry discs",
}
NON_ADDRESS.update({("espresso_fp.c", value): "frsqrte mantissa lookup table"
                    for value in (0x02FB7000, 0x02D26000, 0x02AC0000, 0x02881000,
                                  0x02665000, 0x02468000, 0x02287000, 0x020C1000)})
TRIVIA = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
LITERAL = re.compile(r'\b0[xX]([0-9a-fA-F]{1,8})(?![0-9a-fA-F])[uUlL]*\b')
WRAPPED = re.compile(r'\b(GC|GD|GC_OPTIONAL|CANONICAL_CODE_ID|guest_code_unchanged)\s*\([^;]*?\)', re.S)
# Built only into the decomp build (cmake/Decomp.cmake), which runs the USA game only: its guest
# addresses are USA addresses by definition and have no EUR counterpart to map.
DECOMP_BUILD_ONLY = ("decomp/", "hostrt/", "native_draw/", "cheapib/", "drawoff/")  # native_draw/, drawoff/: only the
# decomp fast build; cheapib/: only the decomp build (cmake/Decomp.cmake wwhd_decomp_link).
# The same boundary inside a shared file: `#if WWHD_DECOMP` / `#ifdef WWHD_DECOMP` blocks (that define is set only on
# the decomp build's game target), and `#if WWHD_PORT_POINTS` blocks (runtime/src/port_points.h: the decomp build's
# port points, also compiled into the unit tests that drive them, never into the normal build). Their `#else` branch is
# normal-build code and is checked.
DECOMP_GUARD = re.compile(r'[ \t]*#[ \t]*(?:if|ifdef)[ \t]+(?:WWHD_DECOMP|WWHD_PORT_POINTS)[ \t]*')
CONDITIONAL = re.compile(r'[ \t]*#[ \t]*(if|ifdef|ifndef|elif|else|endif)\b')
FIXED_GAME_ALLOCATIONS = {0x145AC92C, 0x3BB9DE00, 0x44BDF900, 0x44BDFD00}
# g_dComIfG_gameInfo members the Japanese build moved: USA offsets 0x5AB0 to the end of the object. An offset in that
# range is written GI(0x...) (guest_addr.h), which gives the build's own; the play pointer can come from anywhere (a
# variable, a call, a hook's argument), so the literal itself is what is checked. Numbers in the range that are not
# play offsets added as they stand, or are mapped through GI() where they are used, are listed with the reason.
#
# Limits of this source audit (a regex scan, not a C++ parser or constant evaluator):
# - bare decimal constants not directly added to a play/gameInfo expression (e.g. `constexpr uint32_t kLink = 23348;`)
#   are missed, to avoid false positives on unrelated counts or millisecond limits (such as 20000);
# - dComIfG_play_c-relative offsets (gameInfo + 0x12A0, so 0x4810..0x5057) are flagged only when a play/bgs/gameInfo
#   identifier or 0x12A0 / 0x1046F0B0 appears nearby, because sound ids and angles share that range;
# - multi-step or indirect split arithmetic across variables or macros (e.g. `kBase + 0x134` or `GI(0x5134) + off`)
#   is not folded.
MOVED_PLAY_OFFSETS = (0x5AB0, 0x62F8)
PLAY_C_BASE = 0x12A0
MOVED_PLAY_C_OFFSETS = (MOVED_PLAY_OFFSETS[0] - PLAY_C_BASE, MOVED_PLAY_OFFSETS[1] - PLAY_C_BASE)
GI_CALL = re.compile(r'\bGI\s*\(')
PLAY_CONTEXT = re.compile(
    r'(?:(?<![A-Za-z])play|(?<![A-Z])Play)(?![a-z])[A-Za-z0-9_]*\b'
    r'|\b(?:[A-Za-z0-9_]*_)?(?:bgs|kBgs|game_?[Ii]nfo)\b'
    r'|\b0[xX](?:12[aA]0|1046[fF]0[bB]0)[uUlL]*\b'
)
PLAY_ADD_DECIMAL = re.compile(
    r'(?:'
    r'(?:(?<![A-Za-z])play|(?<![A-Z])Play)(?![a-z])[A-Za-z0-9_]*'
    r'|\b(?:[A-Za-z0-9_]*_)?(?:bgs|kBgs|game_?[Ii]nfo)'
    r'|\bGD\s*\(\s*0[xX]1046[fF]0[bB]0[uUlL]*\s*\)'
    r'|\b0[xX]12[aA]0[uUlL]*'
    r')(?:\s*\(\s*\))?(?:\s*\))?\s*\+\s*(\d+)[uUlL]*\b(?!\s*(?:[.eEfF*\/%]|<<|>>))'
)
SPLIT_HEX_ADD = re.compile(
    r'\b0[xX]([0-9a-fA-F]{1,8})[uUlL]*\s*\+\s*(?:0[xX]([0-9a-fA-F]{1,8})|(\d+))[uUlL]*\b'
    r'(?!\s*(?:[.eEfF*\/%]|<<|>>))'
)
NOT_A_MOVED_OFFSET = {
    **{("src/editor/demo_names.cpp", v): "a demo id" for v in (0x6103, 0x612E, 0x6165, 0x6166, 0x619A, 0x61AB, 0x61B1,
                                                                0x61E4, 0x61FE)},
    ("src/hle/padscore.cpp", 0x5FE0): "KPADGetMplsWorkSize's result",
    ("src/mods/native_actor_carry.cpp", 0x5B38): "a bound: the play pointer leaves room for the canonical object",
    ("src/mods/story_particles.cpp", 0x5AB4): "a bound: the play pointer leaves room for the canonical object",
    ("src/mods/story_world_grapple.cpp", 0x5B38): "a bound: the play pointer leaves room for the canonical object",
    **{("src/mods/play_context.h", v): "a canonical offset the reader maps (Addresses::member, GI())"
       for v in (0x5B2C, 0x5BB2, 0x5BB3, 0x5BB5, 0x5BB6, 0x5BB7, 0x5BBB, 0x5CD8, 0x5CDC, 0x5CEA)},
    **{("src/mods/story_game_objects.cpp", v): "a canonical offset, read as GI(l.pointer)"
       for v in (0x5AB8, 0x5ABC, 0x5AC0, 0x5AC4)},
    ("src/mods/story_notebook.cpp", 0x6000): "an angle (s16)",
}



def without_decomp_blocks(text):
    """TEXT with the lines of decomp-build-only conditional blocks blanked (line numbers kept)."""
    lines = text.split("\n")
    stack = []  # per open conditional: True while its lines are decomp-build only
    for i, line in enumerate(lines):
        m = CONDITIONAL.match(line)
        if m and m[1] in ("if", "ifdef", "ifndef"):
            stack.append(bool(DECOMP_GUARD.fullmatch(line)))
        elif m and m[1] in ("elif", "else") and stack:
            stack[-1] = False
        elif m and m[1] == "endif" and stack:
            stack.pop()
        elif any(stack):
            lines[i] = ""
    return "\n".join(lines)


def address_errors(source, name, mapping):
    text = TRIVIA.sub(lambda m: re.sub(r"[^\n]", " ", m[0]), source)
    text = without_decomp_blocks(text)
    spans = [(m.start(), m.end(), "code" if m[1] in ("GC", "GC_OPTIONAL", "CANONICAL_CODE_ID", "guest_code_unchanged") else "data")
             for m in WRAPPED.finditer(text)]
    errors = []
    for m in LITERAL.finditer(text):
        a = int(m[1], 16)
        if a in FIXED_GAME_ALLOCATIONS:
            line = text.count("\n", 0, m.start()) + 1
            errors.append(f"{name}:{line}: fixed game allocation; use a mapped global or program identity")
            continue
        kind = "code" if 0x02000000 <= a < 0x03000000 else "data" if 0x10000000 <= a < 0x10500000 else None
        # MEM2's base is a VM boundary throughout the runtime, never a global.
        if kind is None or a == 0x10000000 or (name, a) in NON_ADDRESS:
            continue
        line = text.count("\n", 0, m.start()) + 1
        if not any(lo <= m.start() < hi and wrapper == kind for lo, hi, wrapper in spans):
            errors.append(f"{name}:{line}: {a:08X} bypasses GC/GD")
        if not valid_address(mapping, kind, a):
            errors.append(f"{name}:{line}: {a:08X} has no {mapping.name} mapping")
        # Check the full address for declarations such as kPlay + field offset.
        tail = re.match(r'\s*\)?\s*\+\s*(0x[0-9a-fA-F]+)', text[m.end():])
        if tail and not valid_address(mapping, kind, a + int(tail[1], 16)):
            errors.append(f"{name}:{line}: derived address has no {mapping.name} mapping")
        elif tail and kind == "data" and valid_address(mapping, kind, a) and \
                mapping.data(a + int(tail[1], 16)) != mapping.data(a) + int(tail[1], 16):
            # a member the build moved inside its object (the Japanese g_dComIfG_gameInfo): map the member
            errors.append(f"{name}:{line}: {a:08X} + {tail[1]} moves in the {mapping.name} build; write "
                          f"GI({tail[1]}) (guest_addr.h) or GD() of the member")
    return errors


def moved_offset_errors(source, name):
    """Literals in the range of the play object members the Japanese build moved, outside GI(...).

    Also catches play_c-relative literals (0x4810..0x5057, i.e. gameInfo + 0x12A0) near play/bgs/gameInfo,
    decimal literals directly added to a play expression, split hex sums (e.g. 0x5A00 + 0x134) landing in the
    moved range, and GI(0x...) + const expressions that cross a Japanese data-shift boundary.
    """
    text = TRIVIA.sub(lambda m: re.sub(r"[^\n]", " ", m[0]), source)
    text = without_decomp_blocks(text)
    spans = []
    errors = []
    flagged = set()
    jp = builds.by_name("JP")
    gi_base = 0x1046F0B0
    for m in GI_CALL.finditer(text):
        depth = 0
        for k in range(m.end() - 1, len(text)):
            depth += {"(": 1, ")": -1}.get(text[k], 0)
            if not depth:
                spans.append((m.start(), k))
                inner = re.fullmatch(r'0[xX]([0-9a-fA-F]{1,8})[uUlL]*', text[m.end():k].strip())
                tail = inner and re.match(
                    r'\s*([+-])\s*(0[xX][0-9a-fA-F]{1,8}|\d+)[uUlL]*\b(?!\s*(?:[.eEfF*\/%]|<<|>>))',
                    text[k + 1:])
                if tail:
                    base = int(inner[1], 16)
                    delta = int(tail[2], 0) * (1 if tail[1] == "+" else -1)
                    combined = base + delta
                    if (0 <= combined < 0x62F8 and valid_address(jp, "data", gi_base + base) and
                            valid_address(jp, "data", gi_base + combined) and
                            jp.data(gi_base + combined) - jp.data(gi_base + base) != delta):
                        line = text.count("\n", 0, m.start()) + 1
                        errors.append(f"{name}:{line}: GI(0x{base:X}) {tail[1]} {tail[2]} crosses a moved-member "
                                      f"shift (0x{combined:X}); write GI(0x{combined:X})")
                        flagged.add((k + 1 + tail.start(2), k + 1 + tail.end(2)))
                break

    def in_spans(pos, ranges):
        return any(lo <= pos < hi for lo, hi in ranges)

    def play_c_fix(gi_off):
        return f"gameInfo + GI(0x{gi_off:X}) (or play_c + GI(0x{gi_off:X}) - 0x{PLAY_C_BASE:X})"

    for m in LITERAL.finditer(text):
        if in_spans(m.start(), spans) or in_spans(m.start(), flagged):
            continue
        value = int(m[1], 16)
        if MOVED_PLAY_OFFSETS[0] <= value < MOVED_PLAY_OFFSETS[1] and (name, value) not in NOT_A_MOVED_OFFSET:
            line = text.count("\n", 0, m.start()) + 1
            errors.append(f"{name}:{line}: 0x{value:X} is the offset of a play object member the Japanese build "
                          f"moved; write GI(0x{value:X}) (guest_addr.h), or list why it is not one in NOT_A_MOVED_OFFSET")
            flagged.add((m.start(), m.end()))
        elif MOVED_PLAY_C_OFFSETS[0] <= value < MOVED_PLAY_C_OFFSETS[1] and (name, value) not in NOT_A_MOVED_OFFSET:
            ctx = text[max(0, m.start() - 120):m.end() + 120]
            if PLAY_CONTEXT.search(ctx):
                line = text.count("\n", 0, m.start()) + 1
                gi_off = value + PLAY_C_BASE
                errors.append(f"{name}:{line}: 0x{value:X} near play may be a play_c-relative offset of a moved "
                              f"member (gameInfo+0x{gi_off:X}); write {play_c_fix(gi_off)} "
                              f"(guest_addr.h), or list why it is not one in NOT_A_MOVED_OFFSET")
                flagged.add((m.start(), m.end()))

    for m in PLAY_ADD_DECIMAL.finditer(text):
        if in_spans(m.start(1), spans) or in_spans(m.start(1), flagged):
            continue
        value = int(m[1], 10)
        if (MOVED_PLAY_OFFSETS[0] <= value < MOVED_PLAY_OFFSETS[1] or
                MOVED_PLAY_C_OFFSETS[0] <= value < MOVED_PLAY_C_OFFSETS[1]) and (name, value) not in NOT_A_MOVED_OFFSET:
            line = text.count("\n", 0, m.start(1)) + 1
            fix = f"GI(0x{value:X})" if value >= MOVED_PLAY_OFFSETS[0] else play_c_fix(value + PLAY_C_BASE)
            errors.append(f"{name}:{line}: {value} (0x{value:X}) added to play may be the decimal offset of a "
                          f"moved member; write {fix} (guest_addr.h)")
            flagged.add((m.start(1), m.end(1)))

    for m in SPLIT_HEX_ADD.finditer(text):
        if in_spans(m.start(), spans):
            continue
        if any(lo <= m.start() < hi or m.start() <= lo < m.end() for lo, hi in flagged):
            continue
        a = int(m[1], 16)
        b = int(m[2], 16) if m[2] else int(m[3], 10)
        total = a + b
        rhs = f"0x{m[2]}" if m[2] else m[3]
        if MOVED_PLAY_OFFSETS[0] <= total < MOVED_PLAY_OFFSETS[1] and (name, total) not in NOT_A_MOVED_OFFSET:
            line = text.count("\n", 0, m.start()) + 1
            errors.append(f"{name}:{line}: 0x{a:X} + {rhs} (= 0x{total:X}) is the offset of "
                          f"a play object member the Japanese build moved; write GI(0x{total:X}) (guest_addr.h)")
        elif MOVED_PLAY_C_OFFSETS[0] <= total < MOVED_PLAY_C_OFFSETS[1] and (name, total) not in NOT_A_MOVED_OFFSET:
            ctx = text[max(0, m.start() - 120):m.end() + 120]
            if PLAY_CONTEXT.search(ctx):
                line = text.count("\n", 0, m.start()) + 1
                gi_off = total + PLAY_C_BASE
                errors.append(f"{name}:{line}: 0x{a:X} + {rhs} (= 0x{total:X}) near play may be a play_c-relative "
                              f"offset of a moved member (gameInfo+0x{gi_off:X}); write "
                              f"{play_c_fix(gi_off)} (guest_addr.h)")
    return errors


def runtime_sources(*roots):
    for root in roots:
        for path in sorted((ROOT / "runtime" / root).rglob("*")):
            if path.suffix not in (".cpp", ".h", ".c", ".mm"):
                continue
            name = path.relative_to(ROOT / "runtime" / root).as_posix()
            if name.startswith(DECOMP_BUILD_ONLY):
                continue
            yield f"{root}/{name}", name, path.read_text(encoding="utf-8")


def valid_address(mapping, kind, address):
    try:
        getattr(mapping, kind)(address)
        return True
    except ValueError:
        return False


class CoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.mapping = builds.by_name("EU")

    def test_every_active_hook_is_mapped(self):
        entries, _ = builds.read_hooks(builds.hook_files(), self.mapping)
        for site, canon, native, where in entries:
            self.assertTrue(valid_address(self.mapping, "code", canon), where)
            if site:
                self.assertFalse(self.mapping.body_differs(canon), where)
        for address in (0x02715310, 0x02574144):
            self.assertTrue(any(canon == address for _, canon, _, _ in entries))
            self.assertTrue(valid_address(self.mapping, "code", address))
        # Stage child heaps enter this destructor directly during ancestor teardown.
        self.assertEqual(self.mapping.code(0x0275558C), 0x02755E48)

    def test_runtime_game_literals_are_wrapped_and_mapped(self):
        sources = [(name, text) for _, name, text in runtime_sources("src")]
        for build in builds.all_builds():
            if build.canonical:
                continue
            errors = [e for name, text in sources for e in address_errors(text, name, build)]
            self.assertEqual(errors, [], "\n".join(errors))

    def test_moved_members_are_mapped(self):
        jp = builds.by_name("JP")
        self.assertTrue(address_errors("ld32(GD(0x1046F0B0) + 0x5B34);", "new.cpp", jp))
        self.assertEqual(address_errors("ld32(GD(0x1046F0B0) + GI(0x5B34));", "new.cpp", jp), [])
        self.assertEqual(address_errors("ld32(GD(0x1046F0B0) + 0x5134);", "new.cpp", jp), [])  # not moved

    def test_moved_play_offsets_are_written_gi(self):
        errors = [e for path, _, text in runtime_sources("src", "include") for e in moved_offset_errors(text, path)]
        self.assertEqual(errors, [], "\n".join(errors))
        # the play pointer held anywhere: only the offset shows the member
        for raw in ("const uint32_t play = GD(0x1046F0B0); ld32(play + 0x5B34);", "ld32(kPlay + 0x5CD8);",
                    "constexpr uint32_t kLink = 0x5B34;", "ld32(play + GI(0x5B34) + 0x5B34);",
                    "ld32(play_c + 0x4894);", "ld32(play + 0x12A0 + 0x4894);",
                    "ld32(play + 23348);", "ld32(play_c + 18580);",
                    "ld32(play + 0x5A00 + 0x134);", "ld32(play + 0x4800 + 0x94);",
                    "ld32(play + GI(0x5134) + 0xA00);"):
            self.assertTrue(moved_offset_errors(raw, "src/new.cpp"), raw)
        self.assertEqual(len(moved_offset_errors("ld32(play + GI(0x12A0) + 0x4894);", "src/new.cpp")), 1)
        for play_c_case in ("ld32(play_c + 0x4894);", "ld32(play_c + 18580);", "ld32(play_c + 0x4800 + 0x94);"):
            errs = moved_offset_errors(play_c_case, "src/new.cpp")
            self.assertEqual(len(errs), 1, play_c_case)
            self.assertIn("gameInfo + GI(0x5B34) (or play_c + GI(0x5B34) - 0x12A0)", errs[0])
        for fine in ("ld32(play + GI(0x5B34));", "ld32(play + GI(pad * 16 + 0x5CD8));", "ld32(play + 0x5134);",
                     "ld32(link + 0x65B0);", "// play + 0x5B34",
                     "uint32_t se = 0x4810;", "if (display && angle == 0x5000) {}",
                     "if (player && timeout > 20000) {}", "ld8(play + GI(0x5BBB) + 1);",
                     "ld32(play + GI(0x5134) + 4);"):
            self.assertEqual(moved_offset_errors(fine, "src/new.cpp"), [], fine)

    def test_canonical_dispatch_ids_are_explicit_and_mapped(self):
        # Canonical metadata must not be region-translated twice. The C++
        # marker is consteval and has no implicit native-pointer conversion.
        self.assertEqual(address_errors(
            "story_native_code_identity::CANONICAL_CODE_ID(0x0254465C).value();",
            "new.cpp", self.mapping), [])
        self.assertTrue(address_errors("uint32_t raw = 0x0254465C;", "new.cpp", self.mapping))
        self.assertTrue(address_errors(
            "CANONICAL_CODE_ID(0x02000000).value();", "new.cpp", self.mapping))
        self.assertTrue(address_errors(
            "CANONICAL_CODE_ID(0x101F84DC).value();", "new.cpp", self.mapping))

    def test_portable_play_fields_keep_their_offsets(self):
        # The cutscene/rope gates and boat restoration derive addresses from kPlay.
        # Mapping only its base is valid only while all those fields share its shift; a field the
        # Japanese build moved is written kPlay + GI(0x...) (guest_addr.h), which maps it too.
        source = (ROOT / "runtime/src/savestate.cpp").read_text(encoding="utf-8")
        base = int(re.search(r'kPlay\s*=\s*GD\(0x([0-9a-fA-F]+)\)', source)[1], 16)
        offsets = {int(m[1], 16) for m in re.finditer(r'\bkPlay\s*\+\s*0x([0-9a-fA-F]+)', source)}
        mapped = {int(m[1], 16) for m in re.finditer(r'\bkPlay\s*\+\s*GI\(0x([0-9a-fA-F]+)\)', source)}
        self.assertGreaterEqual(len(offsets | mapped), 10)
        for build in builds.all_builds():
            for offset in offsets:
                self.assertEqual(build.data(base + offset), build.data(base) + offset,
                                 f"{build.name}: portable-state field +{offset:X} crosses a data shift; "
                                 f"write kPlay + GI(0x{offset:X})")

    def test_decomp_only_blocks_are_the_only_exemption(self):
        raw = "uint32_t site = 0x0254465C;\n"
        for guard in ("#if WWHD_PORT_POINTS  // the points", "#if WWHD_DECOMP", "#ifdef WWHD_DECOMP"):
            self.assertEqual(address_errors(f"{guard}\n{raw}#endif\n", "new.cpp", self.mapping), [], guard)
        # the #else branch, code after the block, and other conditions are the normal build's
        self.assertTrue(address_errors(f"#if WWHD_PORT_POINTS\n#else\n{raw}#endif\n", "new.cpp", self.mapping))
        self.assertTrue(address_errors(f"#if WWHD_PORT_POINTS\n#endif\n{raw}", "new.cpp", self.mapping))
        self.assertTrue(address_errors(f"#if WWHD_PORT_POINTS\n#if X\n#endif\n#endif\n{raw}", "new.cpp", self.mapping))
        self.assertTrue(address_errors(f"#ifndef WWHD_DECOMP\n{raw}#endif\n", "new.cpp", self.mapping))
        self.assertTrue(address_errors(f"#if !WWHD_DECOMP\n{raw}#endif\n", "new.cpp", self.mapping))
        self.assertTrue(address_errors(f"#if WWHD_DECOMP || X\n{raw}#endif\n", "new.cpp", self.mapping))
        self.assertTrue(address_errors(f"#if WWHD_DECOMP_FAST\n{raw}#endif\n", "new.cpp", self.mapping))
        line = address_errors(f"#if WWHD_DECOMP\n{raw}#endif\n\n{raw}", "new.cpp", self.mapping)[0]
        self.assertTrue(line.startswith("new.cpp:5:"), line)

    def test_guard_rejects_raw_and_unmapped_addresses(self):
        self.assertTrue(address_errors("ld32(0x101F84DC);", "new.cpp", self.mapping))
        self.assertTrue(address_errors("GD(0x027200A0)", "new.cpp", self.mapping))
        self.assertTrue(address_errors("GC(0x2593B18);", "new.cpp", self.mapping))
        self.assertTrue(address_errors("GC(0x02593B18);", "new.cpp", self.mapping))
        self.assertTrue(address_errors("GD(0x104FFFF0);", "new.cpp", self.mapping))
        self.assertTrue(address_errors("ld32(0x145AC92C);", "new.cpp", self.mapping))
        self.assertTrue(address_errors("shader == 0x44BDFD00;", "new.cpp", self.mapping))
        self.assertFalse(address_errors("const uint32_t p = GD(0x101F84DC);", "new.cpp", self.mapping))
        self.assertFalse(address_errors("GC(0x027200A0)", "new.cpp", self.mapping))


if __name__ == "__main__":
    unittest.main()
