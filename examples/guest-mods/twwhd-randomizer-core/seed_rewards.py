"""Decode implemented reward constants from a separately validated USA seed.

Schema entries describe PowerPC operands, not executable bytes. The caller must
provide bounded reads after compatibility validation. No instruction is emitted.
"""
# Each site names its low immediate byte, expected opcode, RT/BF field and RA.
REWARDS = {
    'zunari': [(0x022DA5A7, 14, 9, 0)],
    'tingle_first': [(0x022E98EF, 14, 4, 0)],
    'tingle_second': [(0x022E9923, 14, 4, 0)],
    'ankle_statues': [(0x022E9973, 14, 4, 0)],
    'flight_first': [(0x022065FB, 14, 12, 0)],
    'orca_500': [(0x02258737, 14, 30, 0)],
    'great_fairy': [(a, 14, 30, 0) for a in
                    (0x02095B2F, 0x02095B27, 0x02095B7F, 0x02095B4F, 0x02095B57, 0x02095B77)],
    'maggie': [(a, 14, 4, 0) for a in (0x0227C2A3, 0x0227C2DF)],
    'marie': [(0x0223FC7F, 14, 0, 0), (0x0223FD93, 14, 10, 0),
              (0x0223FDC3, 14, 0, 0), (0x0223FEBB, 14, 0, 0)],
    'lenzo': [(a, 14, 10, 0) for a in (0x022CFA77, 0x022CFAAB)],
    'ivan': [(0x02298BFF, 14, 4, 0)],
    'battlesquid': [(0x02267187, 14, 7, 0), (0x02267197, 14, 8, 0), (0x02267367, 14, 10, 0)],
    'tree': [(0x02347AAB, 14, 4, 0)],
    'beedle': [(a, 11, 0, 31) for a in (0x0221557F, 0x02215587, 0x0221558F)],
}
RAW_REWARDS = {'dampa': [(0x028FA240, 1)], 'doc_bandam': [(0x028FA474, 1), (0x028FA475, 1)]}

# Alternate native branches must describe the same seed reward.
RAW_ALIASES = [(REWARDS['zunari'][0], 0x028FA2D4)]
ALIASES = [(REWARDS['zunari'][0], (0x022DA95B, 11, 0, 12)), (REWARDS['tree'][0], (0x02347C2B, 14, 4, 0))] + [
    (site, (address, 10, 0, 31)) for site, address in
    zip(REWARDS['beedle'], (0x022155BB, 0x022155AF, 0x022155C3))]


def extract_rewards(read):
    def operand(site):
        address, opcode, first, base = site
        data = read(address - 3, 4)
        if len(data) != 4:
            raise ValueError(f'short reward operand at {address:08X}')
        word = int.from_bytes(data, 'big')
        if (word >> 26, (word >> 21) & 31, (word >> 16) & 31, (word >> 8) & 255) != (opcode, first, base, 0):
            raise ValueError(f'unsupported reward operand at {address:08X}')
        return word & 255
    result = {name: [operand(site) for site in sites] for name, sites in REWARDS.items()}
    for primary, alias in ALIASES:
        if operand(primary) != operand(alias):
            raise ValueError(f'inconsistent reward operands at {primary[0]:08X}/{alias[0]:08X}')
    for primary, address in RAW_ALIASES:
        data = read(address, 1)
        if len(data) != 1 or data[0] != operand(primary):
            raise ValueError(f'inconsistent reward byte at {address:08X}')
    for name, sites in RAW_REWARDS.items():
        values = []
        for address, size in sites:
            data = read(address, size)
            if len(data) != size:
                raise ValueError(f'short reward byte at {address:08X}')
            values.append(int.from_bytes(data, 'big'))
        result[name] = values
    return result
