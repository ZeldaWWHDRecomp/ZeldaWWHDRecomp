"""Read the reviewed original-game reward table bytes from a validated USA seed.

These are data locations used by the source port, not executable operands or a
blanket RPX delta. Remaining reward families need their own reviewed consumers.
"""
REWARD_TABLE_ADDRESSES = (
    0x1001C194, 0x1001C195, 0x1001C196,
    0x1003150C, 0x10031518, 0x10031524, 0x10031530, 0x1003153C,
    0x10031548, 0x10031554, 0x10031560, 0x1003156C, 0x10031578,
    0x10031584, 0x10031590, 0x1003159C,
    0x1019016D, 0x10190175, 0x1019017D, 0x10190185, 0x1019018D,
    0x101C3873, 0x101C3877, 0x101C387B, 0x101C387F,
    0x101C399B, 0x101C399F, 0x101C39A3, 0x101C39A7, 0x101C39AB,
    0x101C39AF, 0x101C546F, 0x101C63BF,
    0x101EAEA7, 0x101EAF57, 0x101EAF67, 0x101EAF77,
)


def extract_reward_tables(read):
    values = []
    for address in REWARD_TABLE_ADDRESSES:
        data = read(address, 1)
        if len(data) != 1:
            raise ValueError(f'short reward table byte at {address:08X}')
        values.append(data[0])
    return values


def render_reward_tables(values):
    if len(values) != len(REWARD_TABLE_ADDRESSES) or any(type(value) is not int or not 0 <= value <= 255 for value in values):
        raise ValueError('invalid reward table data')
    lines = ['/* Private player-owned reward table data. */',
             'static void randomizer_apply_reward_data(void) {']
    for address, value in zip(REWARD_TABLE_ADDRESSES, values):
        lines.append(f'    WWHD_GAME_DATA(0x{address:08X},u8)=0x{value:02X};')
    lines.append('}')
    return '\n'.join(lines)+'\n'

# Invariant MIT upstream one-time reward metadata. These high halves are
# not item bytes and must never silently substitute for altered seed data.
WINDFALL_REWARD_FLAGS = ((0x101C399C,0x6904),(0x101C39A0,0x6908),(0x101C39AC,0x6910))
def validate_windfall_reward_flags(read):
    for address, expected in WINDFALL_REWARD_FLAGS:
        if read(address,2) != expected.to_bytes(2,'big'):
            raise ValueError(f'unsupported Windfall reward flag at {address:08X}')
