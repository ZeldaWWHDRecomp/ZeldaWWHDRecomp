#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Audit and optionally translate a hash-pinned USA randomizer output for a private test build.

Ordinary setup and the supported-build registry are deliberately unchanged. This developer tool
requires the player's original USA game too, reports hooks in changed functions, refuses changed
hook instructions, and requires explicit review of every overlapping function before generation.
No game bytes are written to the JSON report. Generated C remains the player's private game data.
"""
import argparse
import bisect
import json
from pathlib import Path
import re
import xml.etree.ElementTree as ET

import builds
from recomp import Recompiler

RANDOMIZER_TITLE = '0005000010143599'


def game_rpx(folder, title):
    folder = Path(folder).resolve()
    if not (folder/'content').is_dir():
        raise ValueError('game folder needs content/')
    actual = ET.parse(folder/'meta/meta.xml').getroot().findtext('title_id', '').strip().lower()
    if actual != title:
        raise ValueError('expected title %s, got %s' % (title, actual))
    path = folder/'code/cking.rpx'
    if not path.is_file():
        raise ValueError('game folder needs code/cking.rpx')
    return path


def outside_inputs(path, folders):
    path = Path(path).resolve()
    for folder in folders:
        folder = Path(folder).resolve()
        if path == folder or folder in path.parents:
            raise ValueError('experiment output must be outside both input game folders: %s' % path)
    return path


def check_layout(base, patched):
    # Hooks refer to USA code/data addresses, not relocated sections. Section growth is
    # allowed (the randomizer appends helpers); shrinking or moving a loaded section is not.
    for name, original in base.p.rpx.by_name.items():
        if not original.flags & 2 or not original.size:  # ELF SHF_ALLOC
            continue
        current = patched.p.rpx.by_name.get(name)
        if current is None or current.addr != original.addr or current.size < original.size:
            raise ValueError('loaded section %s moved, shrank or disappeared; USA hooks need a new map' % name)
    if base.p.entry != patched.p.entry:
        raise ValueError('entry point changed; requires a separate startup review')


def audit(base, patched, hooks):
    check_layout(base, patched)
    original = base.p.rpx.by_name['.text']
    current = patched.p.rpx.by_name['.text']
    changed = {original.addr+i for i, (a, b) in enumerate(zip(original.data, current.data)) if a != b}
    functions, unknown = set(), []
    for address in changed:
        index = bisect.bisect_right(base.sorted_entries, address)-1
        if index < 0 or address >= base.func_end(base.sorted_entries[index]):
            unknown.append(address)
        else:
            functions.add(base.sorted_entries[index])
    overlaps, changed_instructions = [], []
    for site, canon, address, where in hooks:
        instruction_changed = any(address+i in changed for i in range(4))
        index = bisect.bisect_right(base.sorted_entries, address)-1
        function = base.sorted_entries[index] if index >= 0 else None
        if function in functions or instruction_changed:
            entry = dict(kind='instruction' if site else 'function', address='%08X' % canon,
                         function='%08X' % function if function is not None else None,
                         instruction_changed=instruction_changed, source=where)
            overlaps.append(entry)
            if instruction_changed:
                changed_instructions.append(entry)
    return dict(original_text_size=original.size, randomized_text_size=current.size,
                appended_text_bytes=current.size-original.size, changed_original_text_bytes=len(changed),
                changed_original_functions=['%08X' % f for f in sorted(functions)],
                unmapped_changed_text_bytes=len(unknown), hooks_in_changed_functions=overlaps,
                changed_hook_instructions=changed_instructions)


def require_review(report, reviewed):
    if report['unmapped_changed_text_bytes']:
        raise ValueError('changed text outside the reference function map; inspect the audit')
    if report['changed_hook_instructions']:
        raise ValueError('a hook instruction changed; port/review that hook before generating')
    required = {h['function'] for h in report['hooks_in_changed_functions']}
    if reviewed != required:
        raise ValueError('review the hook overlaps, then pass exactly --reviewed-hook-functions %s (got %s)' %
                         (','.join(sorted(required)), ','.join(sorted(reviewed))))


def run(args):
    folders = (Path(args.base_game).resolve(), Path(args.game).resolve())
    # A dump can use symlinked code/content directories or a symlinked RPX.
    # Protect their resolved targets as well as the game-folder paths.
    protected = folders + tuple((folder/part).resolve() for folder in folders for part in
                                ('code', 'content', 'meta', 'code/cking.rpx', 'meta/meta.xml', 'code/app.xml'))
    report_path = outside_inputs(args.report, protected)
    outdir = outside_inputs(args.outdir, protected) if args.outdir else None
    if not args.audit_only and outdir is None:
        raise ValueError('--outdir is required unless --audit-only is selected')
    expected = args.sha256.lower()
    if not re.fullmatch(r'[0-9a-f]{64}', expected):
        raise ValueError('--sha256 must be the full SHA-256 of this randomized RPX')
    base_path = game_rpx(folders[0], builds.canonical_build().title_id)
    patched_path = game_rpx(folders[1], RANDOMIZER_TITLE)
    base_build = builds.identify(str(base_path))
    if base_build is None or base_build.name != 'USA':
        raise ValueError('--base-game must be the recognized, unmodified USA game')
    actual = builds.file_sha256(patched_path)
    if actual != expected:
        raise ValueError('randomized RPX SHA-256 mismatch: expected %s, got %s' % (expected, actual))
    # A private identity map, never registered as a supported game. No mutation of the
    # ordinary registry or the installer, and the generated runtime gets the right title.
    patched_build = builds.Build(dict(name='USA', title_id=RANDOMIZER_TITLE, rpx_sha256=actual))
    base = Recompiler(str(base_path))
    patched = Recompiler(str(patched_path), build=patched_build, preserve_registers=True, mod_hooks=args.mod_hooks)
    hooks, _ = builds.read_hooks(builds.hook_files(), base_build)
    report = audit(base, patched, hooks)
    report.update(format=1, experimental=True, base_sha256=base_build.sha256, randomized_sha256=actual,
                  title_id=RANDOMIZER_TITLE, preserve_registers=True, mod_hooks=args.mod_hooks, generated=False)
    reviewed = {a.strip().upper() for a in args.reviewed_hook_functions.split(',') if a.strip()}
    report['reviewed_hook_functions'] = sorted(reviewed)
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print('%d changed original functions; %d overlapping hook points; %d changed hook instructions' %
          (len(report['changed_original_functions']), len(report['hooks_in_changed_functions']),
           len(report['changed_hook_instructions'])), flush=True)
    print('Audit: %s' % report_path, flush=True)
    if not args.audit_only:
        require_review(report, reviewed)
        patched.run(str(outdir), args.insns_per_file)
        if builds.file_sha256(patched_path) != actual or builds.file_sha256(base_path) != base_build.sha256:
            raise ValueError('an input RPX changed during translation; discard this experiment output')
        report['generated'] = True
        report_path.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--base-game', required=True, help='private original USA game folder')
    ap.add_argument('--game', required=True, help='private randomized code/content/meta folder')
    ap.add_argument('--sha256', required=True, help='exact randomized code/cking.rpx SHA-256')
    ap.add_argument('--report', required=True, help='JSON audit destination outside the input folders')
    ap.add_argument('--outdir', help='private generated C destination, outside the input folders')
    ap.add_argument('--audit-only', action='store_true')
    ap.add_argument('--mod-hooks', action='store_true', help='also enable existing mod-SDK hook dispatch in generated game code')
    ap.add_argument('--reviewed-hook-functions', default='', help='comma-separated audited USA function entries')
    ap.add_argument('--insns-per-file', type=int, default=30000)
    args = ap.parse_args()
    try:
        run(args)
    except (ValueError, OSError, ET.ParseError) as error:
        ap.exit(1, '%s\n' % error)


if __name__ == '__main__':
    main()
