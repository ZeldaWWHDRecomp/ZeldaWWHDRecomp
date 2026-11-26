#!/usr/bin/env python3
"""Tests for ci/gitlab/gh_steps.py on synthetic workflows (no network, no builds).

    python3 ci/gitlab/test_gh_steps.py
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import textwrap
import unittest

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import gh_steps  # noqa: E402

WORKFLOW = textwrap.dedent('''
    name: t
    env:
      TOP: top
    jobs:
      main:
        env:
          VERSION: ${{ needs.version.outputs.version }}
          ABI: ${{ matrix.abi }}
        strategy:
          matrix:
            include:
              - abi: a64
                name: arm
              - abi: x64
                name: x86
        steps:
          - uses: actions/checkout@v5
          - id: one
            run: |
              echo "FROM_ENV=hello" >> "$GITHUB_ENV"
              echo "out=$TOP-$VERSION-$ABI-${{ matrix.name }}" >> "$GITHUB_OUTPUT"
              mkdir -p bin && printf '#!/bin/sh\\necho tool-ok\\n' > bin/mytool && chmod +x bin/mytool
              echo "$PWD/bin" >> "$GITHUB_PATH"
          - name: Uses outputs, env and path
            run: |
              test "${{ steps.one.outputs.out }}" = "top-v1.2.3-a64-arm"
              test "$FROM_ENV" = hello
              test "$(mytool)" = tool-ok
              test "${{ secrets.MY_SECRET != '' }}" = false
              test "${{ inputs.devel_build }}" = true
          - name: Skipped by condition
            if: matrix.abi == 'x64'
            run: exit 1
          - name: Composite
            id: comp
            uses: ./act
            with:
              word: bird
          - run: test "${{ steps.comp.outputs.said }}" = "tweet bird"
          - name: Cache
            id: cache
            uses: actions/cache@v5
            with:
              path: cached
              key: k-${{ hashFiles('data/*.txt') }}
          - name: Fill cache
            if: steps.cache.outputs.cache-hit != 'true'
            run: mkdir -p cached && echo filled > cached/f && echo miss >> log.txt
          - run: test "$(cat cached/f)" = filled
          - uses: actions/upload-artifact@v5
            with:
              name: art-${{ matrix.name }}
              path: |
                out/*.bin
              if-no-files-found: error
          - name: Allowed failure
            continue-on-error: true
            run: exit 3
          - name: Still running
            run: echo ok >> log.txt
      fail:
        steps:
          - run: exit 2
          - name: Not after a failure
            run: echo bad >> log.txt
          - name: Always
            if: always()
            run: echo always >> log.txt
      consume:
        if: needs.version.outputs.publish == 'true'
        steps:
          - uses: actions/download-artifact@v5
            with:
              pattern: art-*
              path: dist
              merge-multiple: true
          - run: test -f dist/x.bin
      timeout:
        steps:
          - timeout-minutes: 0.02
            run: sleep 30
''')

ACTION = textwrap.dedent('''
    name: act
    inputs:
      word:
        required: true
    outputs:
      said:
        value: ${{ steps.s.outputs.said }}
    runs:
      using: composite
      steps:
        - id: s
          shell: bash
          run: echo "said=tweet ${{ inputs.word }}" >> "$GITHUB_OUTPUT"
''')


class GhSteps(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.ws = Path(self.tmp.name)
        (self.ws / 'wf.yml').write_text(WORKFLOW)
        (self.ws / 'act').mkdir()
        (self.ws / 'act' / 'action.yml').write_text(ACTION)
        (self.ws / 'data').mkdir()
        (self.ws / 'data' / 'a.txt').write_text('1')
        (self.ws / 'out').mkdir()
        (self.ws / 'out' / 'x.bin').write_text('x')

    def tearDown(self):
        self.tmp.cleanup()

    def run_job(self, *args, extra_env=None):
        env = {k: v for k, v in os.environ.items() if not k.startswith(('GH_', 'CI_', 'GITLAB'))}
        env.update(CI_PROJECT_DIR=str(self.ws), GH_NEEDS_VERSION_VERSION='v1.2.3',
                   GH_INPUT_DEVEL_BUILD='true', **(extra_env or {}))
        return subprocess.run([sys.executable, str(HERE / 'gh_steps.py'), 'wf.yml', *args], cwd=self.ws,
                              env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)

    def log(self):
        p = self.ws / 'log.txt'
        return p.read_text().split() if p.exists() else []

    def test_main_job_and_cache_reuse(self):
        r = self.run_job('main', '--matrix', 'abi=a64')
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn('Skipped by condition: skipped, condition false', r.stdout)
        self.assertEqual(self.log(), ['miss', 'ok'])
        self.assertTrue((self.ws / '.gitlab-ci-artifacts' / 'art-arm' / 'x.bin').is_file())
        # second pipeline: GitLab restored .gitlab-ci-cache, the workspace is fresh
        import shutil
        shutil.rmtree(self.ws / 'cached')
        (self.ws / 'log.txt').unlink()
        r = self.run_job('main', '--matrix', 'abi=a64')
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn('cache hit', r.stdout)
        self.assertEqual(self.log(), ['ok'])
        # a changed hashFiles() input is a different key: miss, and the old entry is replaced
        (self.ws / 'data' / 'a.txt').write_text('2')
        shutil.rmtree(self.ws / 'cached')
        (self.ws / 'log.txt').unlink()
        r = self.run_job('main', '--matrix', 'abi=a64')
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertEqual(self.log(), ['miss', 'ok'])
        self.assertEqual(len(list((self.ws / '.gitlab-ci-cache').glob('*/meta.json'))), 1)

    def test_failure_stops_later_steps_but_not_always(self):
        r = self.run_job('fail')
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertEqual(self.log(), ['always'])

    def test_job_condition_and_download(self):
        r = self.run_job('consume')
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn('job condition false', r.stdout)
        self.run_job('main', '--matrix', 'abi=a64')
        r = self.run_job('consume', extra_env={'GH_NEEDS_VERSION_PUBLISH': 'true'})
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertTrue((self.ws / 'dist' / 'x.bin').is_file())

    def test_step_timeout(self):
        r = self.run_job('timeout')
        self.assertEqual(r.returncode, 1, r.stdout)
        self.assertIn('timed out', r.stdout)

    def test_skip_and_unknown_action(self):
        r = self.run_job('main', '--matrix', 'abi=a64', '--skip-step', '^Composite$', '--dry-run')
        self.assertIn('skipped on GitLab', r.stdout)
        (self.ws / 'wf.yml').write_text('jobs:\n  j:\n    steps:\n      - uses: someone/new-action@v1\n')
        r = self.run_job('j')
        self.assertEqual(r.returncode, 1)
        self.assertIn('has no GitLab handler', r.stdout)


class Expressions(unittest.TestCase):
    def ev(self, text, **ctx):
        base = {'env': {}, 'steps': {}, 'github': {'ref': 'refs/heads/devel', 'event_name': 'push'}}
        base.update(ctx)
        return gh_steps.Evaluator(base, {'always': lambda: True, 'startswith': lambda a, b: a.startswith(b)}).eval(text)

    def test_operators(self):
        self.assertTrue(self.ev("github.event_name == 'PUSH'"))   # case-insensitive, as on GitHub
        self.assertTrue(self.ev("always() && github.event_name == 'push' && (github.ref == 'refs/heads/devel' || github.ref == 'refs/heads/main')"))
        self.assertEqual(self.ev("env.X || 'fallback'"), 'fallback')
        self.assertTrue(self.ev("steps.x.outputs.cache-hit != 'true'"))
        self.assertFalse(self.ev("!startsWith(github.ref, 'refs/heads/')"))


if __name__ == '__main__':
    unittest.main()
