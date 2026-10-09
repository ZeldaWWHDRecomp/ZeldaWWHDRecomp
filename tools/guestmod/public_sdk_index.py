#!/usr/bin/env python3
"""Inventory SDK declarations from a clean, public HD decomp checkout.

Reads source as data; never imports or executes code from the input repository.
The inventory deliberately contains no function bodies or initialized game data.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess

PUBLIC_URL = 'https://github.com/ZeldaWWHDDecomp/wwhd'
VERIFY = re.compile(r'\bVERIFY\(\s*(0x[0-9A-Fa-f]+)\s*,\s*(&?[A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*\)')
DECL = re.compile(r'(?:^|[\n;}])\s*(?:(?:static|inline|extern)\s+)*([\w:<>,*& \t\n]+?[ \t\n*&])([A-Za-z_]\w*(?:::[~A-Za-z_]\w*)*)\s*\(([^;{}]*)\)\s*(?:const\s*)?\{')
LAYOUT = re.compile(r'\bWWHD_(SIZE|OFFSET)\(\s*([^;\n]+?)\s*\)\s*;')


def git(root, *args):
    return subprocess.check_output(['git', '-C', str(root), *args], text=True).strip()



def mask_declarations(text):
    masked = re.sub(r'/\*.*?\*/|//[^\n]*', lambda m: ''.join('\n' if c == '\n' else ' ' for c in m[0]), text, flags=re.S)
    lines, continuation = [], False
    for line in masked.splitlines(keepends=True):
        directive = continuation or line.lstrip().startswith('#')
        continuation = directive and line.rstrip().endswith('\\')
        lines.append(''.join('\n' if c == '\n' else ' ' for c in line) if directive else line)
    return ''.join(lines)


def inventory(root):
    root = root.resolve()
    remote = git(root, 'remote', 'get-url', 'origin').removesuffix('.git').rstrip('/')
    if remote != PUBLIC_URL:
        raise ValueError('input must be a fresh HTTPS clone of the public HD decomp')
    if git(root, 'status', '--porcelain', '--untracked-files=all'):
        raise ValueError('public source checkout must be clean')
    source = root / 'wwhd_src'
    functions, layouts, unresolved = [], [], []
    for path in sorted(source.rglob('*')):
        if path.suffix not in ('.cpp', '.h'):
            continue
        text = path.read_text()
        relative = path.relative_to(root).as_posix()
        # Preserve positions while masking comments before scanning declarations.
        masked = mask_declarations(text)
        declarations = {}
        for match in DECL.finditer(masked):
            declarations.setdefault(match[2], []).append(match)
        for match in VERIFY.finditer(text):
            target = match[2]
            alias = re.search(r'\b' + re.escape(target) + r'\s*=\s*(&[\w:]+)\s*;', masked[:match.start()])
            if alias:
                target = alias[1]
            candidates = [d for d in declarations.get(target.lstrip('&'), []) if d.start() < match.start()]
            if not candidates and '::' in match[2] and not match[2].startswith('&'):
                candidates = [d for d in declarations.get(match[2].split('::')[-1], []) if d.start() < match.start()]
            if not candidates:
                unresolved.append({'address': int(match[1], 16), 'name': match[2], 'source': relative})
                continue
            decl = candidates[-1]
            parameters = ' '.join(decl[3].split())
            if target.startswith('&'):
                owner = target[1:].rsplit('::', 1)[0]
                parameters = owner + '* self' + (', ' + parameters if parameters else '')
            functions.append({'address': int(match[1], 16), 'name': match[2],
                              'return': ' '.join(decl[1].split()),
                              'parameters': parameters, 'source': relative})
        for match in LAYOUT.finditer(text):
            layouts.append({'kind': match[1].lower(), 'arguments': match[2], 'source': relative})
    functions.sort(key=lambda f: (f['address'], f['name'], f['source']))
    from public_sdk_abi import aliases
    return {'abi_aliases': aliases(root), 'public_source': PUBLIC_URL, 'revision': git(root, 'rev-parse', 'HEAD'),
            'functions': functions, 'layout_assertions': layouts, 'unresolved_declarations': unresolved}



def symbols_header(index):
    """Every verified public name gets an address; ambiguous names keep address suffixes."""
    entries = index['functions'] + index['unresolved_declarations']
    names = {}
    for entry in entries:
        name = re.sub(r'[^A-Za-z0-9_]', '_', entry['name'].lstrip('&'))
        names.setdefault(name, set()).add(entry['address'])
    lines = ['/* Generated from ' + index['public_source'],
             ' * Revision: ' + index['revision'],
             ' * Regenerate: python3 tools/guestmod/regenerate_sdk.py --public-clone build/public-wwhd',
             ' * Public decomp notices: CC0-1.0; see public-wwhd-LICENSE.',
             ' * USA version 0 addresses. Contains declarations only. */', '#pragma once', '']
    for name, addresses in sorted(names.items()):
        for address in sorted(addresses):
            lines.append(f'#define WWHD_ADDR_{name}_{address:08X} 0x{address:08X}')
        if len(addresses) == 1:
            address = next(iter(addresses))
            lines.append(f'#define WWHD_ADDR_{name} WWHD_ADDR_{name}_{address:08X}')
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('public_clone', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--symbols-header', type=Path)
    parser.add_argument('--layouts-dir', type=Path)
    args = parser.parse_args()
    try:
        result = inventory(args.public_clone)
    except (ValueError, subprocess.CalledProcessError) as exc:
        parser.error(str(exc))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(result, indent=2) + '\n')
    if args.layouts_dir:
        from public_sdk_layouts import generate
        generate(args.public_clone, args.layouts_dir, result['revision'])
        from public_sdk_bindings import bindings, save_view
        declarations, skipped = bindings(result)
        (args.layouts_dir / 'bindings.h').write_text(declarations)
        (args.layouts_dir / 'save.h').write_text(save_view(args.public_clone, result['revision']))
        from public_sdk_data import declarations
        (args.layouts_dir / 'data.h').write_text(declarations(
            lambda source: (args.public_clone / 'wwhd_src' / source).read_text(), result['revision']))
        print(f'Typed declarations: {len(result["functions"])-len(skipped)}; unsupported signatures: {len(skipped)}')
    if args.symbols_header:
        args.symbols_header.parent.mkdir(parents=True, exist_ok=True)
        args.symbols_header.write_text(symbols_header(result))
        (args.symbols_header.parent / 'public-wwhd-LICENSE').write_bytes((args.public_clone / 'LICENSE').read_bytes())
    print(f"Indexed {len(result['functions'])} functions and {len(result['layout_assertions'])} layout assertions; {len(result['unresolved_declarations'])} declarations need resolution")


if __name__ == '__main__':
    main()
