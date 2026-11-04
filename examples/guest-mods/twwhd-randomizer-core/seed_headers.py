"""Render decoded seed data as C declarations, never executable instructions.

Compatibility validation and asset preparation are separate. Story flags must
come from the source port's reviewed initialization policy, not an RPX code dump.
"""
import math
from seed_startup import BYTE_FIELDS, validate_startup
from seed_rewards import REWARDS, RAW_REWARDS
from initialization_policy import INITIAL_STORY_FLAGS


def _integer(value, maximum):
    if type(value) is not int or not 0 <= value <= maximum:
        raise ValueError('seed integer is outside its supported range')
    return str(value)


def render_startup(configuration, story_flags=INITIAL_STORY_FLAGS):
    scalar_keys = set(BYTE_FIELDS) | {
        'fix_helmaroc_rng', 'invert_sea_compass_x_axis', 'instant_text_boxes', 'reveal_full_sea_chart',
        'entrances_shuffled', 'mesa_door_unlocked', 'quarter_hearts', 'start_room',
        'start_spawn', 'last_korl_hint_message_number',
    }
    if set(configuration) != scalar_keys | {'starting_gear', 'damage_multiplier'}:
        raise ValueError('missing or unknown startup fields')
    validate_startup(configuration)
    declarations = ['/* Private player-owned seed startup data. */']
    for name in sorted(scalar_keys):
        if name in ('instant_text_boxes', 'invert_sea_compass_x_axis', 'fix_helmaroc_rng'):
            limit = 1
        elif name == 'last_korl_hint_message_number':
            limit = 0x7FFFFFFF
        elif name == 'quarter_hearts':
            limit = 65535
        else:
            limit = 255
        if name in ('invert_sea_compass_x_axis', 'fix_helmaroc_rng'):
            declarations.append(f'#define init_{name} {_integer(configuration[name], limit)}')
        else:
            declarations.append(f'static const u32 init_{name} = {_integer(configuration[name], limit)};')
    gear = configuration['starting_gear']
    if not isinstance(gear, (list, tuple)) or len(gear) > 256:
        raise ValueError('invalid starting gear list')
    declarations.append('static const u8 init_starting_gear[] = {' +
                        ','.join([_integer(item, 254) for item in gear] + ['255']) + '};')
    multiplier = configuration['damage_multiplier']
    if type(multiplier) is not float or not math.isfinite(multiplier) or not 0 <= multiplier <= 80:
        raise ValueError('invalid damage multiplier')
    declarations.append(f'static const f32 init_damage_multiplier = {multiplier.hex()}f;')
    if not isinstance(story_flags, (list, tuple)) or not 1 <= len(story_flags) <= 1024:
        raise ValueError('invalid initialization event policy')
    declarations.append('static const u16 init_story_flags[] = {' +
                        ','.join(_integer(flag, 65535) for flag in story_flags) + '};')
    return '\n'.join(declarations) + '\n'


def render_rewards(rewards):
    schema = {**REWARDS, **RAW_REWARDS}
    if set(rewards) != set(schema):
        raise ValueError('missing or unknown reward groups')
    declarations = ['/* Private player-owned seed reward data. */']
    for name, sites in schema.items():
        values = rewards[name]
        if not isinstance(values, (list, tuple)) or len(values) != len(sites):
            raise ValueError('incorrect seed reward group length')
        data = [_integer(value, 255) for value in values]
        if len(data) == 1 and name != 'zunari':
            declarations.append(f'static const u32 init_reward_{name} = {data[0]};')
        else:
            declarations.append(f'static const u8 init_reward_{name}[] = {{' + ','.join(data) + '};')
    return '\n'.join(declarations) + '\n'


def render_music_policy(value):
    if type(value) is not int or value not in (0, 1):
        raise ValueError('unsupported optional music policy')
    return (f'#define WWHD_RANDOMIZER_REMOVE_MUSIC {value}\n'
            'static const u32 init_remove_music = WWHD_RANDOMIZER_REMOVE_MUSIC;\n')


def render_particle_policy(value):
    if type(value) is not int or value not in (0, 1):
        raise ValueError('invalid particle policy')
    return (f'#define WWHD_RANDOMIZER_PARTICLE_PERFORMANCE {value}\n'
            'static const u32 init_particle_performance = WWHD_RANDOMIZER_PARTICLE_PERFORMANCE;\n')

def render_quiet_sail(value):
    if type(value) is not int or value not in (0, 1):
        raise ValueError('unsupported quiet Swift Sail policy')
    return f'#define WWHD_RANDOMIZER_QUIET_SWIFT_SAIL {value}\n'


def render_classic_policy(value):
    if type(value) is not int or value not in (0, 1):
        raise ValueError('unsupported classic mode policy')
    return f'#define WWHD_RANDOMIZER_CLASSIC_MODE {value}\n'
