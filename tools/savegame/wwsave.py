"""Wind Waker save layouts: GameCube memory-card (.gci) and Wii U HD (cking.sav).

Shared module of gc2hd.py / hd2gc.py / hd_save_info.py. Plain Python, no game data: only
structure layouts, checksums and enum names (from the zeldaret tww decompilation and this
project's verified WWHD decompilation).

Sources
-------
GameCube (tww, GZLE01/GZLJ01 identical; d_save.h only differs for the demo build):
  include/d/d_save.h            dSv_player_*_c, dSv_memBit_c, dSv_ocean_c, dSv_event_c, ...
  src/d/d_save.cpp              dSv_info_c::memory_to_card / card_to_memory (packed order)
  include/m_Do/m_Do_MemCardRWmng.h, src/m_Do/m_Do_MemCardRWmng.cpp
                                card_savedata / card_gamedata, the three checksums
HD (wwhd_src on decomp-actors, all functions verified against the binary):
  d/d_save_5.cpp                dSv_info_c::card_to_memory 025BA7B0 / memory_to_card 025BA9FC
                                (kCardHead: the same packed block order and sizes),
                                initdata_to_card 025BAC50
  d/d_menu_save_storage.cpp     SaveMgr file sections (read/write 027246A0..0272516C)
  d/d_menu_save_card.cpp        slot checksum 02721AC4 / 02721AFC, initial card 027219AC
  HD-only per-slot data (save + 0x12C0 area): init 0271FF4C -> 0271FBE8 (player, 16 B),
  0271FAA8 (status, 4 B), 0271F8C0 (event, 20 B), 027265E0 (name, UTF-16, default "Link"),
  02720848/027207B0 (map, 220 B, zero); file trailer: format word 4 (0271FF4C stores it at
  +0x4D0 = save+0x1790) and the CRC-32 of the extra data (027201E4 -> 0273B264).
"""
import struct
import unicodedata
import zlib

# --------------------------------------------------------------------------------------------
# Packed per-file save data (dSv_save_c_PACKED, 0x768 bytes) as both games write it to the card.
# Each entry: (name, offset, size). Two tables, written down independently from the two sources;
# they agree (checked at import), and every copy goes field by field through these tables.

def _blocks(spec):
    out, off = [], 0
    for name, size in spec:
        out.append((name, off, size))
        off += size
    return out, off

# GameCube: dSv_info_c::memory_to_card (tww src/d/d_save.cpp), sizes from include/d/d_save.h
_GC_BLOCKS, _GC_SIZE = _blocks([
    ("status_a", 0x18), ("status_b", 0x18), ("return_place", 0xC), ("item", 0x15),
    ("get_item", 0x15), ("item_record", 8), ("item_max", 8), ("bag_item", 0x18),
    ("get_bag_item", 0xC), ("bag_item_record", 0x18), ("collect", 0xD), ("map", 0x84),
    ("info", 0x5C), ("config", 5), ("priest", 0x10)] +
    [("status_c%d" % i, 0x70) for i in range(4)] +
    [("memory%02d" % i, 0x24) for i in range(16)] +
    [("ocean", 0x64), ("event", 0x100), ("reserve", 0x50)])

# HD: kCardHead + the explicit copies of dSv_info_c::card_to_memory 025BA7B0 (wwhd_src/d/d_save_5.cpp)
_HD_BLOCKS, _HD_SIZE = _blocks([
    ("status_a", 0x18), ("status_b", 0x18), ("return_place", 0xC), ("item", 0x15),
    ("get_item", 0x15), ("item_record", 8), ("item_max", 8), ("bag_item", 0x18),
    ("get_bag_item", 0xC), ("bag_item_record", 0x18), ("collect", 0xD), ("map", 0x84),
    ("info", 0x5C), ("config", 5), ("priest", 0x10)] +
    [("status_c%d" % i, 0x70) for i in range(4)] +
    [("memory%02d" % i, 0x24) for i in range(16)] +
    [("ocean", 0x64), ("event", 0x100), ("reserve", 0x50)])

# Fields inside the blocks: (block, name, offset in block, size). Same structs in both games.
_STATUS_A = [("max_life", 0, 2), ("life", 2, 2), ("rupee", 4, 2), ("field_6", 6, 2), ("field_8", 8, 1),
             ("select_item", 9, 5), ("select_equip", 0xE, 4), ("wallet_size", 0x12, 1),
             ("max_magic", 0x13, 1), ("magic", 0x14, 1), ("field_15", 0x15, 1), ("field_16", 0x16, 1),
             ("pad_17", 0x17, 1)]
_FIELDS = {
    "status_a": _STATUS_A,
    "status_b": [("date_ipl", 0, 8), ("field_8", 8, 4), ("time", 0xC, 4), ("date", 0x10, 2),
                 ("tact_wind_x", 0x12, 2), ("tact_wind_y", 0x14, 2), ("pad_16", 0x16, 2)],
    "return_place": [("stage", 0, 8), ("room", 8, 1), ("point", 9, 1), ("unk_a", 0xA, 1), ("unk_b", 0xB, 1)],
    "item": [("slot%02d" % i, i, 1) for i in range(21)],
    "get_item": [("slot%02d" % i, i, 1) for i in range(21)],
    "item_record": [("timer", 0, 2), ("picture_num", 2, 1), ("arrow_num", 3, 1), ("bomb_num", 4, 1),
                    ("bottle_num", 5, 3)],
    "item_max": [("reserved1", 0, 1), ("arrow_max", 1, 1), ("bomb_max", 2, 1), ("field_3", 3, 5)],
    "bag_item": [("beast", 0, 8), ("bait", 8, 8), ("reserve", 0x10, 8)],
    "get_bag_item": [("reserve_flags", 0, 4), ("beast_flags", 4, 1), ("bait_flags", 5, 1), ("unk_6", 6, 6)],
    "bag_item_record": [("beast_num", 0, 8), ("bait_num", 8, 8), ("reserve_num", 0x10, 8)],
    "collect": [("collect", 0, 8), ("field_8", 8, 1), ("tact", 9, 1), ("triforce", 0xA, 1),
                ("symbol", 0xB, 1), ("field_c", 0xC, 1)],
    "map": [("arrive_grid", 0, 0x10), ("get_map", 0x10, 0x10), ("open_map", 0x20, 0x10),
            ("complete_map", 0x30, 0x10), ("fmap_bits", 0x40, 49), ("field_71", 0x71, 16),
            ("field_81", 0x81, 1), ("field_82", 0x82, 2)],
    "info": [("field_0", 0, 0x10), ("save_count", 0x10, 2), ("death_count", 0x12, 2),
             ("player_name", 0x14, 17), ("name_25", 0x25, 17), ("name_36", 0x36, 17),
             ("puzzle", 0x47, 17), ("clear_count", 0x58, 1), ("random_salvage", 0x59, 1),
             ("field_5a", 0x5A, 2)],
    "config": [("ruby", 0, 1), ("sound_mode", 1, 1), ("attention_type", 2, 1), ("vibration", 3, 1),
               ("field_4", 4, 1)],
    "priest": [("pos", 0, 12), ("angle", 0xC, 2), ("room", 0xE, 1), ("field_f", 0xF, 1)],
    "ocean": [("ocean", 0, 0x64)],
    "event": [("flags", 0, 0x100)],
    "reserve": [("reserve", 0, 0x50)],
}
_STATUS_C = ([("a_" + n, o, s) for n, o, s in _STATUS_A] +
             [("item%02d" % i, 0x18 + i, 1) for i in range(21)] +
             [("picture_num", 0x2D, 1), ("arrow_num", 0x2E, 1), ("bomb_num", 0x2F, 1),
              ("reserved1_max", 0x30, 1), ("arrow_max", 0x31, 1), ("bomb_max", 0x32, 1),
              ("beast", 0x33, 8), ("bait", 0x3B, 8), ("reserve", 0x43, 8),
              ("beast_num", 0x4B, 8), ("bait_num", 0x53, 8), ("reserve_num", 0x5B, 8),
              ("collect", 0x63, 8), ("c_field_8", 0x6B, 1), ("tact", 0x6C, 1), ("triforce", 0x6D, 1),
              ("symbol", 0x6E, 1), ("c_field_c", 0x6F, 1)])
_MEMORY = [("tbox", 0, 4), ("switch", 4, 16), ("item", 0x14, 4), ("visited_room", 0x18, 8),
           ("key_num", 0x20, 1), ("dungeon_item", 0x21, 1), ("pad_22", 0x22, 2)]
for _i in range(4):
    _FIELDS["status_c%d" % _i] = _STATUS_C
for _i in range(16):
    _FIELDS["memory%02d" % _i] = _MEMORY


def _field_table(blocks):
    tab = {}
    for blk, boff, bsize in blocks:
        used = 0
        for name, off, size in _FIELDS[blk]:
            assert off == used and off + size <= bsize, (blk, name)
            used = off + size
            tab[blk + "." + name] = (boff + off, size)
        assert used == bsize, blk
    return tab

GC_FIELDS = _field_table(_GC_BLOCKS)
HD_FIELDS = _field_table(_HD_BLOCKS)
GAMEDATA_SIZE = 0x768
assert _GC_SIZE == _HD_SIZE == GAMEDATA_SIZE
assert GC_FIELDS.keys() == HD_FIELDS.keys()
FIELD_NAMES = list(GC_FIELDS)  # in card order


def get(data, table, name):
    off, size = table[name]
    return data[off:off + size]


def put(buf, table, name, value):
    off, size = table[name]
    assert len(value) == size, (name, len(value), size)
    buf[off:off + size] = value

# --------------------------------------------------------------------------------------------
# GameCube .gci (one memory-card file with its 0x40-byte directory entry in front)

GCI_HEADER = 0x40
CARD_BLOCK = 0x2000
GC_REGIONS = {b"GZLE": "USA (GZLE01)", b"GZLJ": "Japan (GZLJ01)", b"GZLP": "Europe (GZLP01)"}
SUPPORTED_GC = (b"GZLE01", b"GZLJ01")


class GciError(Exception):
    pass


def gc_checksum_card(block):
    """mDoMemCdRWm_CalcCheckSum: big-endian u16 sums over the 0x1FFC bytes before the word"""
    c0 = c1 = 0
    for (v,) in struct.iter_unpack(">H", block[:0x1FFC]):
        c0 = (c0 + v) & 0xFFFF
        c1 = (c1 + (~v & 0xFFFF)) & 0xFFFF
    return (c0 << 16) | c1


def gc_checksum_gamedata(data):
    """mDoMemCdRWm_CalcCheckSumGameData: byte sum and complement sum, u64"""
    c0 = sum(data) & 0xFFFFFFFF
    c1 = (len(data) * 0xFFFFFFFF - c0) & 0xFFFFFFFF  # sum of (~b) as u32
    return (c0 << 32) | c1


def read_gci(raw, allow=SUPPORTED_GC):
    """returns (gamecode, [slot gamedata bytes or None if neither copy has a valid checksum])"""
    if len(raw) < GCI_HEADER + 3 * CARD_BLOCK:
        raise GciError("not a Wind Waker .gci (too short: %d bytes)" % len(raw))
    code = raw[0:6]
    if raw[0:4] not in GC_REGIONS:
        raise GciError("not a Wind Waker save (game code %r)" % code)
    if code not in allow:
        raise GciError("unsupported region %s: only %s are supported (PAL saves store the language "
                       "in the save counter and were not checked)" %
                       (GC_REGIONS[raw[0:4]], ", ".join(c.decode() for c in allow)))
    if raw[8:15] != b"gczelda":
        raise GciError("unexpected file name %r (expected gczelda)" % raw[8:40].rstrip(b"\0"))
    copies = [raw[GCI_HEADER + CARD_BLOCK * k:GCI_HEADER + CARD_BLOCK * (k + 1)] for k in (1, 2)]
    slots = []
    for s in range(3):
        chosen = None
        for blk in copies:  # mDoMemCdRWm_Restore: the first copy, the second if it is damaged
            g = blk[8 + s * 0x770:8 + (s + 1) * 0x770]
            if struct.unpack(">Q", g[0x768:0x770])[0] == gc_checksum_gamedata(g[:0x768]):
                chosen = g[:0x768]
                break
        slots.append(chosen)
    return code, slots


def gc_slot_is_new(g):
    """GameCube file select (d_file_select.cpp): a file without a player name is 'New Game'"""
    return get(g, GC_FIELDS, "info.player_name")[0] == 0


def write_gci(code, slots, comment=b"Zelda: The Wind Waker", template=None):
    """a .gci with the three slots. Without a template the banner/icon block is blank (the card
    file still loads; the IPL just shows no picture)."""
    blocks = 12
    out = bytearray(GCI_HEADER + blocks * CARD_BLOCK)
    if template is not None:
        out[0:GCI_HEADER + CARD_BLOCK] = template[0:GCI_HEADER + CARD_BLOCK]
    else:
        # DEntry: game code, maker, 0xFF, banner format, file name, mtime, image offset,
        # icon format, animation speed, permissions, copy count, first block, block count,
        # 0xFFFF, comment offset (values as in the game's own files, block 0 holds the comment)
        out[0:6] = code
        out[6] = 0xFF
        out[7] = 1
        out[8:15] = b"gczelda"
        struct.pack_into(">IIHHBBHHHI", out, 0x28, 0, 0, 1, 3, 4, 0, 0, blocks, 0xFFFF, 0x1C00)
        c = GCI_HEADER + 0x1C00
        out[c:c + len(comment)] = comment
    sav = bytearray(CARD_BLOCK)
    struct.pack_into(">II", sav, 0, 1, 0)
    for s, g in enumerate(slots):
        o = 8 + s * 0x770
        sav[o:o + 0x768] = g
        struct.pack_into(">Q", sav, o + 0x768, gc_checksum_gamedata(g))
    struct.pack_into(">I", sav, 0x1FFC, gc_checksum_card(sav))
    for k in (1, 2):
        out[GCI_HEADER + k * CARD_BLOCK:GCI_HEADER + (k + 1) * CARD_BLOCK] = sav
    return bytes(out)

# --------------------------------------------------------------------------------------------
# Wii U HD cking.sav

HD_SLOT = 0xA94          # per file: game data, unused rest, checksum pair at +0xA8C/+0xA90
HD_SLOT_USED = 0xA8C
HD_EXTRA = [("player", 16), ("status", 4), ("event", 20), ("name", 18), ("map", 220)]
HD_EXTRA_SIZE = 3 * sum(s for _, s in HD_EXTRA)          # 0x342
HD_FORMAT_WORD = 4
HD_FILE_SIZE = 3 * HD_SLOT + HD_EXTRA_SIZE + 8          # 8966


class HdError(Exception):
    pass


def hd_slot_checksum(block):
    """SaveMgr 02721AC4: byte sum and complement sum (u32 each) over 0xA8C bytes"""
    s = sum(block[:HD_SLOT_USED]) & 0xFFFFFFFF
    return s, (HD_SLOT_USED * 0xFFFFFFFF - s) & 0xFFFFFFFF


def hd_extra_defaults():
    """the HD-only per-file data of a fresh file (0271FF4C)"""
    player = bytearray(16)
    player[5] = 1                                   # 0271FBE8
    return {"player": bytes(player), "status": bytes(4), "event": bytes(20),
            "name": "Link", "map": bytes(220)}


def _name_utf16(name):
    b = name.encode("utf-16-be")[:16]
    return (b + b"\0\0").ljust(18, b"\0")


def _name_from_utf16(b):
    out = []
    for i in range(0, 18, 2):
        ch = b[i:i + 2]
        if ch == b"\0\0" or len(ch) < 2:
            break
        out.append(ch.decode("utf-16-be", "replace"))
    return "".join(out)


def hd_extra_crc(extras):
    """027201E4: CRC-32 over the extra sections; each name contributes only (length+1) BYTES of its
    UTF-16 text (the game passes the character count to a byte copy), zero padded to 18"""
    buf = bytearray()
    for sect, size in HD_EXTRA:
        for s in range(3):
            v = extras[s][sect]
            if sect == "name":
                raw = _name_utf16(v) if isinstance(v, str) else v
                n = 0
                while n < 9 and raw[2 * n:2 * n + 2] != b"\0\0":
                    n += 1
                buf += raw[:n + 1].ljust(18, b"\0")
            else:
                buf += v
    assert len(buf) == HD_EXTRA_SIZE
    return zlib.crc32(bytes(buf)) & 0xFFFFFFFF


def read_hd(raw):
    """returns ([slot gamedata], [extras dict per slot], info dict)"""
    if len(raw) != HD_FILE_SIZE:
        raise HdError("cking.sav must be %d bytes, got %d" % (HD_FILE_SIZE, len(raw)))
    slots, extras = [], [dict() for _ in range(3)]
    for s in range(3):
        blk = raw[s * HD_SLOT:(s + 1) * HD_SLOT]
        if struct.unpack(">II", blk[HD_SLOT_USED:HD_SLOT]) != hd_slot_checksum(blk):
            raise HdError("slot %d checksum mismatch" % s)
        slots.append(blk[:GAMEDATA_SIZE])
    o = 3 * HD_SLOT
    for sect, size in HD_EXTRA:
        for s in range(3):
            v = raw[o:o + size]
            extras[s][sect] = v
            o += size
    fmt, crc = struct.unpack(">II", raw[o:o + 8])
    if fmt != HD_FORMAT_WORD:
        raise HdError("format word %d (expected %d)" % (fmt, HD_FORMAT_WORD))
    if crc != hd_extra_crc(extras):
        raise HdError("extra-data CRC mismatch")
    return slots, extras, {"format": fmt, "crc": crc}


def write_hd(slots, extras):
    out = bytearray()
    for g in slots:
        blk = bytearray(HD_SLOT)
        blk[:GAMEDATA_SIZE] = g                     # 0x768..0xA8C stay zero, as the game leaves them
        struct.pack_into(">II", blk, HD_SLOT_USED, *hd_slot_checksum(blk))
        out += blk
    for sect, size in HD_EXTRA:
        for s in range(3):
            v = extras[s][sect]
            if sect == "name" and isinstance(v, str):
                v = _name_utf16(v)
            assert len(v) == size
            out += v
    out += struct.pack(">II", HD_FORMAT_WORD, hd_extra_crc(extras))
    assert len(out) == HD_FILE_SIZE
    return bytes(out)


def hd_name(extras_slot):
    v = extras_slot["name"]
    return v if isinstance(v, str) else _name_from_utf16(v)

# --------------------------------------------------------------------------------------------
# player names

def decode_gc_name(raw, code):
    """the GC player name (info.player_name, NUL-terminated, ASCII for USA, Shift-JIS for Japan)"""
    raw = raw.split(b"\0", 1)[0]
    if code[3:4] == b"J":
        return raw.decode("shift_jis", "replace")
    return raw.decode("latin-1")


def hd_safe_name(name):
    """the HD name area holds 8 UTF-16 characters. The USA HD font is Latin only, so full-width
    letters are folded to ASCII (NFKC) and anything still non-Latin falls back to 'Link'."""
    folded = unicodedata.normalize("NFKC", name)
    if folded and all(0x20 <= ord(c) < 0x250 for c in folded):
        return folded[:8], None
    return "Link", "name %r is not Latin; the USA HD font cannot show it, using 'Link'" % name

# --------------------------------------------------------------------------------------------
# names for hd_save_info (enum names from tww include/d/d_item_data.h, d_save.h)

ITEM_NAMES = {
    0x20: "Telescope", 0x21: "Tingle Tuner (GC) / Tingle Bottle (HD)", 0x22: "Wind Waker", 0x23: "Picto Box",
    0x24: "Spoils Bag", 0x25: "Grappling Hook", 0x26: "Deluxe Picto Box", 0x27: "Hero's Bow",
    0x28: "Power Bracelets", 0x29: "Iron Boots", 0x2A: "Magic Armor", 0x2C: "Bait Bag", 0x2D: "Boomerang",
    0x2F: "Hookshot", 0x30: "Delivery Bag", 0x31: "Bombs", 0x33: "Skull Hammer", 0x34: "Deku Leaf",
    0x35: "Fire & Ice Arrows", 0x36: "Light Arrow", 0x38: "Hero's Sword", 0x39: "Master Sword (powerless)",
    0x3A: "Master Sword (half)", 0x3B: "Hero's Shield", 0x3C: "Mirror Shield", 0x3E: "Master Sword (full)",
    0x42: "Pirate's Charm", 0x43: "Hero's Charm", 0x50: "Empty Bottle", 0x51: "Red Potion",
    0x52: "Green Potion", 0x53: "Blue Potion", 0x54: "Half Soup", 0x55: "Elixir Soup", 0x56: "Water",
    0x57: "Fairy", 0x58: "Forest Firefly", 0x59: "Forest Water", 0x77: "Swift Sail (HD)", 0x78: "Sail",
}
SLOT_NAMES = ["Telescope", "Sail", "Wind Waker", "Grappling Hook", "Spoils Bag", "Boomerang",
              "Deku Leaf", "Tingle Tuner/Bottle", "Picto Box", "Iron Boots", "Magic Armor",
              "Bait Bag", "Bow", "Bombs", "Bottle 1", "Bottle 2", "Bottle 3", "Bottle 4",
              "Delivery Bag", "Hookshot", "Skull Hammer"]
SONGS = ["Wind's Requiem", "Ballad of Gales", "Command Melody", "Earth God's Lyric",
         "Wind God's Aria", "Song of Passing"]
PEARLS = ["Nayru", "Din", "Farore"]
ISLANDS = ("ForsakenFortress StarIsland NorthernFairyIsland GaleIsle CrescentMoonIsland SevenStarIsles "
           "OverlookIsland FourEyeReef MotherandChildIsles SpectacleIsland WindfallIsland PawprintIsle "
           "DragonRoostIsland FlightControlPlatform WesternFairyIsland RockSpireIsle TingleIsland "
           "NorthernTriangleIsland EasternFairyIsland FireMountain StarBeltArchipelago ThreeEyeReef "
           "GreatfishIsle CyclopsReef SixEyeReef ToweroftheGods EasternTriangleIsland ThornedFairyIsland "
           "NeedleRockIsle IsletofSteel StoneWatcherIsland SouthernTriangleIsland PrivateOasis BombIsland "
           "BirdsPeakRock DiamondSteppeIsland FiveEyeReef SharkIsland SouthernFairyIsland IceRingIsle "
           "ForestHaven CliffPlateauIsles HorseshoeIsland OutsetIsland HeadstoneIsland TwoEyeReef "
           "AngularIsles BoatingCourse FiveStarIsles").split()
STAGES = ["Sea", "Sea (alt)", "Forsaken Fortress", "Dragon Roost Cavern", "Forbidden Woods",
          "Tower of the Gods", "Earth Temple", "Wind Temple", "Ganon's Tower", "Hyrule",
          "Ship", "Misc (interiors)", "Sub-dungeons", "Sub-dungeons (new)", "Blue Chu Jelly", "Test"]
DUNGEON_BITS = ["map", "compass", "boss key", "boss defeated", "heart container", "boss intro"]


def repair_medli(raw, file=1):
    """Undo premature sword ownership in an early Dragon Roost save (#85).

    Return a new file; never change the input. Only the chosen Quest Log's sword
    ownership, equipped sword, and two checksum words change. All other bytes,
    including padding and the other Quest Logs, are preserved.
    """
    if file not in (1, 2, 3):
        raise HdError("Quest Log must be 1, 2 or 3")
    slots, _, _ = read_hd(raw)
    g = slots[file - 1]
    field = lambda name: get(g, HD_FIELDS, name)
    events = field("event.flags")
    swords = field("collect.collect")[0]
    # This repair cannot infer earned equipment later in the story. Restrict it
    # to before the first pearl and before Medli's sage awakening.
    if (not int.from_bytes(field("info.save_count"), "big") or
            field("info.clear_count")[0] or field("collect.symbol")[0] or
            field("collect.triforce")[0] or events[0x2E] & 0x04 or
            any(field("memory%02d.dungeon_item" % n)[0] & 8 for n in range(3, 10))):
        raise HdError("repair requires an early first-playthrough save, before the first pearl")
    if not swords & 0x04 or not swords & 0x01 or swords & 0xF0:
        raise HdError("save does not have the premature Master Sword flags that hide Medli")
    if field("status_a.select_equip")[0] not in (0x38, 0x39, 0x3A, 0x3E):
        raise HdError("equipped sword is not recognized; no repair written")
    out = bytearray(raw)
    start = (file - 1) * HD_SLOT
    out[start + HD_FIELDS["collect.collect"][0]] = 0x01
    out[start + HD_FIELDS["status_a.select_equip"][0]] = 0x38
    block = out[start:start + HD_SLOT]
    struct.pack_into(">II", out, start + HD_SLOT_USED, *hd_slot_checksum(block))
    read_hd(out)
    return bytes(out)


def main():
    import argparse
    from pathlib import Path

    ap = argparse.ArgumentParser(description="Wind Waker HD save repair tools (work on copies).")
    sub = ap.add_subparsers(dest="command", required=True)
    p = sub.add_parser("repair-medli", help="repair premature sword flags hiding Medli (#85)")
    p.add_argument("save", type=Path, help="source cking.sav; never modified")
    p.add_argument("--file", type=int, choices=(1, 2, 3), default=1, help="Quest Log (default: 1)")
    p.add_argument("-o", "--output", type=Path, required=True, help="output folder (files must not already exist)")
    a = ap.parse_args()
    try:
        raw = a.save.read_bytes()
        repaired = repair_medli(raw, a.file)
        target = a.output / "cking.sav"
        backup = a.output / "cking.sav.before-medli.bak"
        if target.resolve() == a.save.resolve() or target.exists() or backup.exists():
            raise HdError("choose a new output folder; source and existing files cannot be overwritten")
        a.output.mkdir(parents=True, exist_ok=True)
        # Exclusive creation also protects against a concurrent repair to this folder.
        with backup.open("xb") as f:
            f.write(raw)
        with target.open("xb") as f:
            f.write(repaired)
    except (HdError, OSError) as e:
        ap.exit(1, "%s\n" % e)
    print("%s: Quest Log %d restored to Hero's Sword; other progress unchanged" % (target, a.file))
    print("Original backup: %s (source was not modified)" % backup)


if __name__ == "__main__":
    main()
