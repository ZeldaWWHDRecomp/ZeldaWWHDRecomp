"""Decode the conditional 1.2.0 entrance and Mesa-door policies into booleans.

Upstream tweaks.cpp applies all three sites only for anyEntrancesShuffled().
Only native operands or branches to the pinned MIT helper symbols are accepted;
The Mesa policy is separate because only door shuffling applies that patch.
No executable bytes are included in generated mod data.
"""
# site, helper symbol address, native instruction family/register operands
SITES = ((0x021271A0, 0x028F9B24, 'lbz', 0, 9),
         (0x02129324, 0x028F9B3C, 'lbz', 12, 12),
         (0x021A4548, 0x028F9B54, 'mr', 3, 31))
MESA_SITE = 0x021A4B30


def extract_entrance_options(read):
    enabled = []
    for site, helper, family, target, source in SITES:
        data = read(site, 4)
        if len(data) != 4:
            raise ValueError('short entrance option operand')
        word = int.from_bytes(data, 'big')
        opcode = word >> 26
        if family == 'lbz':
            native = opcode == 34 and (word >> 21) & 31 == target and (word >> 16) & 31 == source and word & 65535 == 0x7B8
        else:
            native = (opcode == 31 and (word >> 21) & 31 == source and (word >> 16) & 31 == target and
                      (word >> 11) & 31 == source and (word >> 1) & 1023 == 444 and not word & 1)
        if native:
            enabled.append(0)
            continue
        displacement = word & 0x03FFFFFC
        if displacement & 0x02000000:
            displacement -= 0x04000000
        if opcode != 18 or word & 3 or site + displacement != helper:
            raise ValueError('unsupported entrance option branch/operand')
        enabled.append(1)
    if len(set(enabled)) != 1:
        raise ValueError('inconsistent entrance option patch group')
    data = read(MESA_SITE, 4)
    if len(data) != 4:
        raise ValueError('short Mesa door option operand')
    word = int.from_bytes(data, 'big')
    # Preserve an ordinary relative, non-linking CR0-EQ branch. Only its two
    # reviewed destinations distinguish the native and door-shuffle policies.
    if (word >> 26 != 16 or (word >> 21) & 31 != 12 or
            (word >> 16) & 31 != 2 or word & 3):
        raise ValueError('unsupported Mesa door option branch')
    displacement = word & 0xFFFC
    if displacement & 0x8000:
        displacement -= 0x10000
    target = MESA_SITE + displacement
    if target not in (0x021A4B8C, 0x021A4C28):
        raise ValueError('unsupported Mesa door option target')
    mesa = int(target == 0x021A4C28)
    if mesa and not enabled[0]:
        raise ValueError('inconsistent Mesa door and entrance options')
    return {'entrances_shuffled': enabled[0], 'mesa_door_unlocked': mesa}
