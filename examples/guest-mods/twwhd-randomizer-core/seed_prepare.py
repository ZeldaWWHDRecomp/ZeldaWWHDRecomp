"""Generate data headers for a separately validated original/seed image pair.

The pure API accepts section-bounded readers after compatibility checks. The
command-line wrapper validates bounded RPX inputs and creates private development
data; it does not build a module or create an installable package.
"""
from extract_boss_rewards import extract as extract_boss_rewards
from seed_headers import render_startup, render_rewards, render_music_policy, render_particle_policy, render_quiet_sail, render_classic_policy
from seed_music_policy import extract_music_policy
from seed_particle_policy import extract_particle_policy
from seed_quiet_sail import extract_quiet_sail
from seed_startup import extract_startup
from seed_text_options import extract_text_options
from seed_compass_options import extract_compass_options
from seed_helmaroc_rng import extract_helmaroc_rng
from seed_rewards import extract_rewards
from seed_resources import extract_resources, render_resources
from seed_reward_tables import extract_reward_tables, render_reward_tables, validate_windfall_reward_flags
from sage_spawn_data import validate_sage_spawns
from fixed_seed_data import validate_fixed_seed_data
from traversal_seed_data import validate_traversal_seed_data
from fast_chest_data import validate_fast_chest_data
from warp_smoke_seed_data import validate_warp_smoke_colors
from seed_classic_policy import extract_classic_policy
from title_seed_data import validate_title_seed_data
from seed_windfall_data import validate_windfall_seed_data
from permanent_resource_policy import validate_permanent_resource_policy


def generate_headers(original_read, seed_read):
    # Decode everything before returning any artifact; a malformed late table
    # cannot leave a partially generated set of headers for a different seed.
    validate_permanent_resource_policy(seed_read)
    validate_windfall_seed_data(original_read, seed_read)
    validate_title_seed_data(seed_read)
    classic = extract_classic_policy(seed_read)
    quiet_sail = extract_quiet_sail(seed_read)
    music_policy = extract_music_policy(seed_read)
    particle_policy = extract_particle_policy(seed_read)
    startup = extract_startup(seed_read)
    startup.update(extract_text_options(original_read, seed_read))
    startup.update(extract_compass_options(original_read, seed_read))
    startup.update(extract_helmaroc_rng(original_read, seed_read))
    rewards = extract_rewards(seed_read)
    bosses = extract_boss_rewards(seed_read)
    resources = extract_resources(original_read, seed_read)
    tables = extract_reward_tables(seed_read)
    boss_header = ('/* Private player-owned boss reward data. */\n'
                   'static const u8 init_reward_boss[6] = {' +
                   ','.join(map(str, bosses)) + '};\n')
    return {
        'seed_init_data.h': render_startup(startup) + render_classic_policy(classic) + render_quiet_sail(quiet_sail) + render_music_policy(music_policy) + render_particle_policy(particle_policy) + render_rewards(rewards),
        'seed_reward_data.h': render_reward_tables(tables),
        'seed_resources.h': render_resources(resources),
        'seed_boss_data.h': boss_header,
    }


def _validated_readers(original_rpx, seed_rpx):
    import sys
    from pathlib import Path
    tools = Path(__file__).resolve().parents[3] / 'tools'
    sys.path[:0] = [str(tools/'recomp'), str(tools)]
    import builds
    import randomizer_compat
    import source_core_compat
    from rpx import Rpx

    original = Rpx(original_rpx, limits=randomizer_compat.RPX_LIMITS)
    if original.buffer_sha256 != builds.canonical_build().sha256:
        raise ValueError('the original game must be the supported unmodified USA build')
    selected = source_core_compat.identify_source_core(seed_rpx)
    seed = Rpx(seed_rpx, limits=randomizer_compat.RPX_LIMITS)
    if seed.buffer_sha256 != selected.sha256:
        raise ValueError('randomized RPX changed before seed data was decoded')

    def reader(image):
        def read(address, size):
            for section in image.sections:
                if section.addr <= address and address+size <= section.addr+len(section.data):
                    return section.data[address-section.addr:address-section.addr+size]
            raise ValueError(f'unmapped seed data at {address:08X}')
        return read
    original_read, seed_read = reader(original), reader(seed)
    validate_sage_spawns(seed_read)
    validate_windfall_reward_flags(seed_read)
    validate_fixed_seed_data(original_read, seed_read)
    validate_traversal_seed_data(original_read, seed_read)
    validate_fast_chest_data(original_read, seed_read)
    validate_warp_smoke_colors(original_read, seed_read)
    return original_read, seed_read, {
        'base_rpx_sha256': original.buffer_sha256,
        'seed_rpx_sha256': seed.buffer_sha256,
        'compatibility': selected.randomizer_report,
    }


def prepare_seed(original_game, randomized_game, output, include_content=False):
    """Prepare private data in a new directory; never alter either input tree.

    Output is development input, not an installable mod. On write failure a
    partial new directory remains without the final preparation record.
    """
    import hashlib
    import json
    from pathlib import Path
    original_game, randomized_game, output = map(Path, (original_game, randomized_game, output))
    target = output.resolve()
    for source in (original_game.resolve(), randomized_game.resolve()):
        if target == source or source in target.parents or target in source.parents:
            raise ValueError('output must be separate from both game folders')
    if output.exists() or output.is_symlink():
        raise FileExistsError('output directory already exists')
    original_read, seed_read, identity = _validated_readers(
        original_game/'code/cking.rpx', randomized_game/'code/cking.rpx')
    headers = generate_headers(original_read, seed_read)
    output.mkdir()
    content = None
    if include_content:
        from seed_content import copy_content_delta
        content = copy_content_delta(original_game/'content', randomized_game/'content', output/'assets')
    for name, text in headers.items():
        with (output/name).open('x', encoding='utf-8', newline='\n') as stream:
            stream.write(text)
    from seed_identity import seed_identity
    record = {
        'format_version': 1, 'kind': 'source-mod-seed-data', 'installable': False,
        **identity,
        'headers': {name: hashlib.sha256(text.encode('utf-8')).hexdigest() for name,text in headers.items()},
        'content': content,
        'seed_identity': seed_identity(identity['seed_rpx_sha256'], content['seed_content_sha256']) if content is not None else None,
    }
    with (output/'seed-preparation.json').open('x', encoding='utf-8') as stream:
        json.dump(record, stream, indent=2); stream.write('\n')
    return record


def main():
    import argparse
    import json
    import struct
    from pathlib import Path
    parser = argparse.ArgumentParser(description='Prepare private source-mod seed data; this does not install a playable randomizer.')
    parser.add_argument('original', type=Path, help='unmodified USA game folder')
    parser.add_argument('randomized', type=Path, help='matching official randomized game folder')
    parser.add_argument('output', type=Path, help='new private output directory')
    parser.add_argument('--content', action='store_true', help='also copy the verified content overlay')
    args = parser.parse_args()
    try:
        record = prepare_seed(args.original, args.randomized, args.output, args.content)
    except (OSError, ValueError, struct.error, IndexError, KeyError) as error:
        parser.exit(1, f'Seed preparation failed: {error}\n')
    print(json.dumps({'seed_rpx_sha256': record['seed_rpx_sha256'], 'seed_identity': record['seed_identity'], 'headers': len(record['headers']),
                      'content_files': len(record['content']['files']) if record['content'] is not None else 0,
                      'installable': False}))


if __name__ == '__main__':
    main()
