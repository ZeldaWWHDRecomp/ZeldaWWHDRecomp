#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""The randomizer's patches for the European and Japanese releases (twwhd_rando/asm_eur, asm_jpn: GreenNaugahyde's
ZeldaWWHDRecompAndroid port, MPL-2.0), checked word by word against the USA patches through this port's own address
maps (tools/recomp/builds/eu.json, jp.json), independently of the fork's mapper.

A patched word is the same as the USA word, or a branch whose target maps, or an address (a whole word, or a lis and
its low half) that maps, or a g_dComIfG_gameInfo member that moved. Relocation entries map their offset and addend.
The randomizer's own code (after the USA .text) moves as one block. Reads repository files only, no game data.
"""
import glob
import os
import re
import sys
import unittest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, '..', 'recomp'))

import builds  # noqa: E402

RANDO = os.path.join(HERE, 'twwhd_rando')
GAME_INFO = 0x1046F0B0
D_FORM = {12, 13, 14, 24, 25, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 48, 49, 50, 51, 52, 53, 54, 55}

# Patch sites inside functions the release changed, where the port's map has no instruction mapping. Each was
# compared by hand with the original instructions of both games (the same instructions around the site):
# make_game_nonlinear's li r3,3; bl at 021B0BC0, and swordless.asm's three hooks into dSv_player_status_a_c (its
# JP version is shorter) with the branches back.
REVIEWED = {
    'EU': {0x021B0BC0: 0x021B0BC0},
    'JP': {0x021B0BC0: 0x021B0BC4,
           0x025B2030: 0x025B2018, 0x025B2034: 0x025B201C,
           0x025B26F4: 0x025B26DC, 0x025B26F8: 0x025B26E0, 0x025B26FC: 0x025B26E4},
}


def parse(path):
    with open(path, encoding='utf-8') as f:
        text = f.read()
    data, relocations = [], []
    block = re.search(r'^Data:\n(.*?)(?=^\w|\Z)', text, re.M | re.S)
    if block:
        for m in re.finditer(r'0x([0-9A-F]+):\s*\[([^\]]*)\]', block.group(1)):
            data.append((int(m.group(1), 16), [int(x, 16) for x in m.group(2).replace('\n', ' ').split(',') if x.strip()]))
    block = re.search(r'^Relocations:\n(.*)', text, re.M | re.S)
    if block:
        for m in re.finditer(r'r_addend:\s*0x([0-9A-F]+),\s*r_info:\s*0x([0-9A-F]+),\s*r_offset:\s*0x([0-9A-F]+)',
                             block.group(1)):
            relocations.append(tuple(int(x, 16) for x in m.groups()))
    return data, relocations


def sext(value, bits):
    return value - (1 << bits) if value & (1 << (bits - 1)) else value


def words(values):
    return [int.from_bytes(bytes(values[k:k + 4]), 'big') for k in range(0, len(values), 4)]


class Checker:
    def __init__(self, folder, build):
        self.folder, self.build = folder, builds.by_name(build)
        self.reviewed = REVIEWED[build]
        with open(os.path.join(RANDO, folder, 'address_map.txt'), encoding='utf-8') as f:
            for line in f:
                if line.startswith('text_end'):
                    self.usa_end, self.end = (int(x, 16) for x in line.split()[1:3])
        self.problems, self.counts = [], {}
        self.his = set()  # every lis rD, hi of the USA patches: a low half may follow a lis in another patch
        for path in glob.glob(os.path.join(RANDO, 'asm', 'patch_diffs', '*.yaml')):
            for at, values in parse(path)[0]:
                if at < 0x10000000 and not len(values) % 4:
                    self.his |= {w & 0xFFFF for w in words(values) if w >> 26 == 15 and (w >> 16) & 31 == 0}

    def map(self, usa):
        if usa in self.reviewed:
            return self.reviewed[usa]
        try:
            if 0x02000000 <= usa < 0x10000000:
                return usa - self.usa_end + self.end if usa >= self.usa_end else self.build.code(usa)
            return self.build.data(usa)
        except ValueError:
            return None

    def ok(self, kind):
        self.counts[kind] = self.counts.get(kind, 0) + 1

    def code(self, name, usa_at, usa, other):
        paired = set()
        for i, (u, o) in enumerate(zip(usa, other)):
            where = '%s %08X' % (name, usa_at + 4 * i)
            if i in paired:
                continue
            if u == o:
                self.ok('same')
                continue
            op = u >> 26
            if op != o >> 26:
                self.problems.append('%s: another instruction' % where)
            elif op == 18 and not u & 2 and u & 3 == o & 3:  # b/bl: the targets map
                target = (usa_at + 4 * i + sext(u & 0x03FFFFFC, 26)) & 0xFFFFFFFF
                if self.map(target) == (self.map(usa_at) + 4 * i + sext(o & 0x03FFFFFC, 26)) & 0xFFFFFFFF:
                    self.ok('branch')
                else:
                    self.problems.append('%s: branch to %08X' % (where, target))
            elif op == 16 and u & ~0xFFFC == o & ~0xFFFC:  # bc
                if self.map(usa_at + 4 * i + sext(u & 0xFFFC, 16)) == self.map(usa_at) + 4 * i + sext(o & 0xFFFC, 16):
                    self.ok('branch')
                else:
                    self.problems.append('%s: conditional branch' % where)
            elif self.map(u) == o:  # an address word (a table in the randomizer's code)
                self.ok('address')
            elif op == 15 and (u >> 16) & 31 == 0 and self.lis_pair(usa, other, i, paired):
                self.ok('address')
            elif op in D_FORM and u & 0xFFFF0000 == o & 0xFFFF0000 and self.low_half(u, o):
                self.ok('address')
            elif op in D_FORM and u & 0xFFFF0000 == o & 0xFFFF0000 and (u >> 16) & 31 and \
                    0x5000 <= u & 0xFFFF < 0x62F8 and \
                    self.build.data(GAME_INFO + (u & 0xFFFF)) - self.build.data(GAME_INFO) == o & 0xFFFF:
                # a member displacement from a pointer register (not r0, which reads as 0): the object's own
                # address shift taken out, as GI() and the guest-mod translator do
                self.ok('gameinfo member')
            else:
                self.problems.append('%s: %08X vs %08X' % (where, u, o))

    @staticmethod
    def value(hi, low_word):
        low = low_word & 0xFFFF
        return ((hi << 16) + (low if low_word >> 26 in (24, 25) else sext(low, 16))) & 0xFFFFFFFF

    def lis_pair(self, usa, other, i, paired):
        register = (usa[i] >> 21) & 31
        for k in range(i + 1, min(i + 10, len(usa))):
            if usa[k] >> 26 in D_FORM and (usa[k] >> 16) & 31 == register:
                if self.map(self.value(usa[i] & 0xFFFF, usa[k])) == self.value(other[i] & 0xFFFF, other[k]):
                    paired.add(k)
                    return True
                return False
        return False

    def low_half(self, u, o):
        return any(self.map(self.value(hi, u)) == self.value(hi, o) for hi in self.his)

    def run(self):
        for usa_path in sorted(glob.glob(os.path.join(RANDO, 'asm', 'patch_diffs', '*.yaml'))):
            name = os.path.basename(usa_path)
            usa_data, usa_relocs = parse(usa_path)
            data, relocs = parse(os.path.join(RANDO, self.folder, 'patch_diffs', name))
            if len(usa_data) != len(data) or len(usa_relocs) != len(relocs):
                self.problems.append('%s: another number of patches' % name)
                continue
            for (usa_at, usa_bytes), (at, values) in zip(usa_data, data):
                if self.map(usa_at) != at or len(usa_bytes) != len(values):
                    self.problems.append('%s %08X: patched at %08X' % (name, usa_at, at))
                elif usa_at >= 0x10000000:
                    for k, (u, o) in enumerate(zip(words(usa_bytes), words(values))):
                        if u == o:
                            self.ok('data')
                        elif self.map(u) == o:
                            self.ok('address')
                        else:
                            self.problems.append('%s data %08X' % (name, usa_at + 4 * k))
                elif len(usa_bytes) % 4:
                    self.problems.append('%s %08X: not whole instructions' % (name, usa_at))
                else:
                    self.code(name, usa_at, words(usa_bytes), words(values))
            for (usa_addend, usa_info, usa_offset), (addend, info, offset) in zip(usa_relocs, relocs):
                base = 0x02000000 if usa_info >> 8 == 1 else 0x10000000  # symbol 1 .text, 2 .data
                if usa_info == info and self.map(usa_offset) == offset and self.map(base + usa_addend) == base + addend:
                    self.ok('relocation')
                else:
                    self.problems.append('%s relocation at %08X' % (name, usa_offset))
        return self


class PortedPatches(unittest.TestCase):
    def check(self, folder, build):
        c = Checker(folder, build).run()
        self.assertEqual(c.problems, [])
        self.assertGreater(c.counts.get('branch', 0), 400)
        self.assertEqual(c.counts.get('relocation'), 290)
        return c

    def test_european_patches(self):
        self.check('asm_eur', 'EU')

    def test_japanese_patches(self):
        c = self.check('asm_jpn', 'JP')
        self.assertEqual(c.counts.get('gameinfo member'), 3)  # custom_funcs/flexible_item_locations: 5B60, 5B64
        self.assertEqual(c.usa_end, 0x028F87F4)
        self.assertEqual(c.end, 0x028F92D4)  # the randomizer's code follows the longer JP .text


class LocationSymbols(unittest.TestCase):
    """Locations whose item is a byte in the randomizer's own code (location_data.yaml, Type: Custom_Symbol; written by
    command/WriteLocations.cpp). That code is at other addresses on the European and Japanese releases, so the symbol
    has to come from the release's own custom_symbols.yaml: a USA address there is inside another function of the
    release (issue #628: the Dampa minigame's item was written into dragon_tingle_statue_item_get_func)."""

    @staticmethod
    def symbols(folder):
        with open(os.path.join(RANDO, folder, 'custom_symbols.yaml'), encoding='utf-8') as f:
            return {m.group(1): int(m.group(2), 16) for m in re.finditer(r'^(\w+):\s*0x([0-9A-Fa-f]+)', f.read(), re.M)}

    @staticmethod
    def location_symbols():
        with open(os.path.join(RANDO, 'logic', 'data', 'location_data.yaml'), encoding='utf-8') as f:
            text = f.read()
        names = []
        for block in re.findall(r'^  SymbolNames:\n((?:    - .*\n)+)', text, re.M):
            names += [n.strip().strip('"') for n in re.findall(r'^    - (.*)$', block, re.M)]
        return [n for n in names if not n.startswith('@')]  # (@address: a USA address, ported like every other)

    def test_releases_have_the_location_symbols_at_their_own_addresses(self):
        names = self.location_symbols()
        self.assertGreaterEqual(len(names), 4)
        usa = self.symbols('asm')
        for folder in ('asm_eur', 'asm_jpn'):
            own = self.symbols(folder)
            for name in names:
                self.assertIn(name, usa)
                self.assertIn(name, own, folder)
                self.assertNotEqual(own[name], usa[name], '%s %s' % (folder, name))

    def test_sources_read_the_symbols_of_the_release(self):
        # only wwhd_rando::asm_dir() (utility/wwhd_region.cpp) names the asm folders
        found = []
        for path in glob.glob(os.path.join(RANDO, '**', '*.cpp'), recursive=True):
            rel = os.path.relpath(path, RANDO).replace(os.sep, '/')
            if rel.startswith('libs/') or rel == 'utility/wwhd_region.cpp':
                continue
            with open(path, encoding='utf-8', errors='replace') as f:
                if re.search(r'"asm(_eur|_jpn)?[/"]', f.read()):
                    found.append(rel)
        self.assertEqual(found, [])


if __name__ == '__main__':
    unittest.main()
