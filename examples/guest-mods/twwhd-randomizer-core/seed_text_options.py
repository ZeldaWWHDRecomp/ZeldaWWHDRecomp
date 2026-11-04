"""Decode the coherent instant-text code option without copying game code.

The original reader must be the validated canonical USA image. Enabled semantic
forms are the public MIT patch's lwz-to-held-input and nop, never executable
bytes emitted by the source mod. Mixed or unfamiliar variants fail closed.
"""

# (site, destination register, base register), from b_button_skips_text.asm.
INPUT_SITES = (
    (0x026FEB1C, 0, 30), (0x026FE628, 10, 8),
    (0x026FCF18, 8, 30), (0x026FD290, 0, 30),
    (0x026FD3DC, 12, 11), (0x026FD9B8, 9, 30),
    (0x026FDB40, 9, 30), (0x026FE818, 8, 30),
    (0x026BA644, 0, 3),
)
ANIMATION_SITES = (0x026F7EA0, 0x026F802C)


def extract_text_options(original_read, seed_read):
    def exact(read, address):
        value = read(address, 4)
        if len(value) != 4:
            raise ValueError('short instant-text option operand')
        return int.from_bytes(value, 'big')

    variants = []
    for address, destination, base in INPUT_SITES:
        original = exact(original_read, address)
        seeded = exact(seed_read, address)
        def load(word, displacement):
            return (word >> 26 == 32 and (word >> 21) & 31 == destination and
                    (word >> 16) & 31 == base and word & 65535 == displacement)
        if not load(original, 0x54):
            raise ValueError('unsupported original instant-text input operand')
        if seeded == original:
            variants.append(0)
        elif load(seeded, 0x50):
            variants.append(1)
        else:
            raise ValueError('unsupported instant-text input operand')
    for address in ANIMATION_SITES:
        original = exact(original_read, address)
        seeded = exact(seed_read, address)
        if original >> 26 != 16:
            raise ValueError('unsupported original instant-text animation guard')
        if seeded == original:
            variants.append(0)
        # Public nop is ori r0,r0,0 (opcode24, RS/RA0, immediate0).
        elif seeded >> 26 == 24 and seeded & 0x3FFFFFF == 0:
            variants.append(1)
        else:
            raise ValueError('unsupported instant-text animation guard')
    if len(set(variants)) != 1:
        raise ValueError('inconsistent instant-text option operands')
    return {'instant_text_boxes': variants[0]}
