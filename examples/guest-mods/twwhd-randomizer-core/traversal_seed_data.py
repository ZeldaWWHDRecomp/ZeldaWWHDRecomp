"""Validate the fixed MIT traversal relocation/data policies before conversion.

No executable bytes are emitted. The source consumers own each animation change;
only the reviewed block halfwords and unique grapple float need startup data.
"""
import struct

# .rela.text addend positions -> (canonical target offset, reviewed new offset).
RELOCATIONS = {
    0x0014EC04: (0x35350, 0x355C4),
    0x0014EC4C: (0x35350, 0x355C4),
    0x00075170: (0x10F10, 0x10FFC),
    0x00153B00: (0x35124, 0x35AAC),
    0x00153B48: (0x35124, 0x35AAC),
    0x00148820: (0x35588, 0x358D8),
    0x001482A4: (0x353A0, 0x35124),
    0x00148430: (0x35124, 0x358D8),
    0x00148310: (0x35410, 0x35AAC),
    0x0014E2D4: (0x358D8, 0x35530),
}
ADDI = {
    0x02170250: (10, 11, 20, 10),
    0x021711D4: (8, 11, 1, 6),
    0x02508B50: (4, 0, 20, 10),
}
FLOAT_TARGETS = {
    0x100355C4: 6.0,
    0x10010FFC: 20.0,
    0x10035AAC: 1.4,
    0x100358D8: 1.6,
    0x10035124: 1.0,
    0x10035530: 2.0,
}
BLOCK_BASE, BLOCK_STRIDE, BLOCK_COUNT = 0x101CB424, 0x9C, 13
GRAPPLE_FLOAT = 0x100110C8


def _read(read, address, size):
    value = read(address, size)
    if len(value) != size:
        raise ValueError(f'short traversal data at {address:08X}')
    return value


def _addi(read, address, first, base, immediate):
    word = int.from_bytes(_read(read, address, 4), 'big')
    if (word >> 26, (word >> 21) & 31, (word >> 16) & 31, word & 65535) != (14, first, base, immediate):
        raise ValueError(f'unsupported traversal addi at {address:08X}')


def validate_traversal_seed_data(original_read, seed_read):
    for address, (old, new) in RELOCATIONS.items():
        if (_read(original_read, address, 4) != old.to_bytes(4, 'big') or
                _read(seed_read, address, 4) != new.to_bytes(4, 'big')):
            raise ValueError(f'unsupported traversal relocation at {address:08X}')
    for address, (first, base, old, new) in ADDI.items():
        _addi(original_read, address, first, base, old)
        _addi(seed_read, address, first, base, new)
    for address, number in FLOAT_TARGETS.items():
        expected = struct.pack('>f', number)
        if (_read(original_read, address, 4) != expected or
                _read(seed_read, address, 4) != expected):
            raise ValueError(f'unsupported traversal float target at {address:08X}')
    if (_read(original_read, GRAPPLE_FLOAT, 4) != struct.pack('>f', 17.0) or
            _read(seed_read, GRAPPLE_FLOAT, 4) != struct.pack('>f', 25.0)):
        raise ValueError('unsupported grapple float')
    for index in range(BLOCK_COUNT):
        address = BLOCK_BASE + index * BLOCK_STRIDE
        expected = bytearray(_read(original_read, address, BLOCK_STRIDE))
        expected[4:6] = (12).to_bytes(2, 'big')
        expected[10:12] = (12).to_bytes(2, 'big')
        if _read(seed_read, address, BLOCK_STRIDE) != expected:
            raise ValueError(f'unsupported traversal block row {index}')
