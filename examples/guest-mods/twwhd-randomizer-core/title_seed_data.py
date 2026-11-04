"""Validate MIT tweaks.cpp::modify_title_screen data, without game bytes."""
import struct

TITLE_FLOATS = ((0x101F7048, 1.4), (0x101F7044, 2.25), (0x10108280, -38.0))


def validate_title_seed_data(seed_read):
    for address, value in TITLE_FLOATS:
        data = seed_read(address, 4)
        if not isinstance(data, (bytes, bytearray, memoryview)) or len(data) != 4:
            raise ValueError('truncated title sparkle data')
        if data != struct.pack('>f', value):
            raise ValueError('unsupported title sparkle data')
