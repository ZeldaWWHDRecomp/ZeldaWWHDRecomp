"""Read supported seed options without copying executable instructions into a mod.

The map initializer contains either its native register move or the randomizer's
boolean immediate. Decode their PowerPC fields and reject unfamiliar variants.
The caller provides a section-bounded reader over the player's private RPX.
"""
REVEAL_CHART_SITE = 0x025B7EA8


def extract_startup_options(read):
    data = read(REVEAL_CHART_SITE, 4)
    if len(data) != 4:
        raise ValueError("short sea-chart initialization operand")
    word = int.from_bytes(data, "big")
    opcode = word >> 26
    first, target, third = (word >> 21) & 31, (word >> 16) & 31, (word >> 11) & 31
    # Native mr r10,r12 is the non-recording OR of r12 with itself.
    if opcode == 31 and (first, target, third) == (12, 10, 12) and ((word >> 1) & 1023) == 444 and not (word & 1):
        enabled = 0
    # Upstream's option writes li r10,1; do not accept arbitrary immediates.
    elif opcode == 14 and (first, target) == (10, 0) and (word & 65535) == 1:
        enabled = 1
    else:
        raise ValueError("unsupported sea-chart initialization option")
    return {"reveal_full_sea_chart": enabled}
