"""Compares the pipelines two runs of the same models-step slice recorded (WWHD_VK_KEY_LOG files).

A hidden run (WWHD_HIDDEN_WINDOWS=1: setup's models step; on Android under its progress screen) and a run that
presents (a visible screen, or WWHD_TEST_PRESENT_HIDDEN=1) must record the same pipelines, or the prepared caches
would not match play. Each key log line is "<ns> <frame> <portable key digest> <key hash> <sweep batch>"
(gfx/vulkan/draw.cpp); the portable digest covers the whole pipeline key: both shaders, the render targets'
formats, the depth format and the fixed-function state.

  python3 tools/installer/model_sweep_keys.py hidden-keys.txt visible-keys.txt
prints the counts and the keys only one run made; exit status 0 when both runs made the same set of pipelines.
"""
import argparse
import sys


def digests(path):
    """The portable key digests of a key log, in order of creation (each once)."""
    seen, order = set(), []
    with open(path, encoding='utf-8', errors='replace') as stream:
        for line in stream:
            parts = line.split()
            if len(parts) >= 3 and len(parts[2]) == 16:
                if parts[2] not in seen:
                    seen.add(parts[2])
                    order.append(parts[2])
    return order


def compare(first, second):
    a, b = set(first), set(second)
    return {'first': len(a), 'second': len(b), 'common': len(a & b),
            'only_first': sorted(a - b), 'only_second': sorted(b - a)}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('first')
    parser.add_argument('second')
    args = parser.parse_args(argv)
    result = compare(digests(args.first), digests(args.second))
    print('%s: %d pipelines; %s: %d pipelines; both: %d' % (args.first, result['first'], args.second,
                                                          result['second'], result['common']))
    for name, keys in (('only in ' + args.first, result['only_first']), ('only in ' + args.second, result['only_second'])):
        if keys:
            print('%s (%d): %s' % (name, len(keys), ' '.join(keys[:20]) + (' ...' if len(keys) > 20 else '')))
    same = not result['only_first'] and not result['only_second']
    print('identical' if same else 'different')
    return 0 if same else 1


if __name__ == '__main__':
    sys.exit(main())
