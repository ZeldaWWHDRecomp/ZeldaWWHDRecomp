"""Extract startup data from a validated TWWHD Randomizer 1.2.0 USA image.

This is a pure decoder, not an RPX loader or compatibility validator. The caller
must validate the build profile and supply a section-bounded reader. Returned
values contain data only; executable instructions are never emitted.
"""
import math
import struct

from seed_options import extract_startup_options
from seed_entrance_options import extract_entrance_options

BYTE_FIELDS = {
    "should_start_with_heros_clothes": 0x028F8D0C,
    "swordless": 0x028F8D0D,
    "skip_rematch_bosses": 0x028F8D0E,
    "starting_magic": 0x028F8E12,
    "progressive_magic_always_double": 0x028F8E13,
    "outset_pig_color": 0x028F8E14,
    "target_type_preference": 0x028F8E15,
    "camera_preference": 0x028F8E16,
    "first_person_camera_preference": 0x028F8E17,
    "gyroscope_preference": 0x028F8E18,
    "ui_display_preference": 0x028F8E19,
    "open_drc": 0x028F8E1A,
    "use_different_korl_hyrule_text": 0x028F9F5C,
}


def validate_startup(configuration):
    """Check the supported 1.2.0 option domains before compiling declarations."""
    for name in BYTE_FIELDS:
        value = configuration[name]
        maximum = 32 if name == 'starting_magic' else 3 if name == 'outset_pig_color' else 1
        if type(value) is not int or not 0 <= value <= maximum:
            raise ValueError(f'unsupported startup option: {name}')
    for name, minimum, maximum in (('quarter_hearts', 4, 80), ('start_room', 1, 49),
                                   ('start_spawn', 0, 255), ('reveal_full_sea_chart', 0, 1), ('entrances_shuffled', 0, 1), ('mesa_door_unlocked', 0, 1),
                                   ('last_korl_hint_message_number', 0xD73, 0x7FFFFFFF)):
        value = configuration[name]
        if type(value) is not int or not minimum <= value <= maximum:
            raise ValueError(f'unsupported startup option: {name}')
    if configuration['mesa_door_unlocked'] and not configuration['entrances_shuffled']:
        raise ValueError('Mesa door option requires shuffled entrances')


def extract_startup(read):
    def exact(address, size):
        value = read(address, size)
        if len(value) != size:
            raise ValueError(f"short startup field at {address:08X}")
        return value

    result = {key: exact(address, 1)[0] for key, address in BYTE_FIELDS.items()}
    result.update(extract_startup_options(exact))
    result.update(extract_entrance_options(exact))
    result["last_korl_hint_message_number"] = int.from_bytes(exact(0x028F9F24, 4), "big")
    multiplier = struct.unpack(">f", exact(0x028FAC24, 4))[0]
    if not math.isfinite(multiplier) or not 0 <= multiplier <= 80:
        raise ValueError("unsupported damage multiplier")
    result["damage_multiplier"] = multiplier
    result["quarter_hearts"] = int.from_bytes(exact(0x028F8E10, 2), "big")
    gear = exact(0x028F8D0F, 257)
    if 255 not in gear:
        raise ValueError("unterminated starting gear")
    result["starting_gear"] = list(gear[:gear.index(255)])
    for key, addresses in (
        ("start_room", (0x025B508C, 0x025B50C8)),
        ("start_spawn", (0x025B5090, 0x025B50CC)),
    ):
        values = []
        for address in addresses:
            word = int.from_bytes(exact(address, 4), "big")
            # addi with RA=0 (li), nonnegative unsigned-byte room/spawn value.
            if word >> 26 != 14 or (word >> 16) & 31 or word & 0xFF00:
                raise ValueError(f"unsupported {key} operand")
            values.append(word & 255)
        if values[0] != values[1]:
            raise ValueError(f"inconsistent {key} operands")
        result[key] = values[0]
    if not 1 <= result["start_room"] <= 49:
        raise ValueError("invalid starting sea room")
    validate_startup(result)
    return result
