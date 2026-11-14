#!/usr/bin/env python3
"""Run one job of a GitHub Actions workflow on a GitLab runner (dormant disaster-recovery CI).

The GitLab pipeline (.gitlab-ci.yml) does not copy the build steps of .github/workflows: each GitLab job
calls this script, which reads the GitHub workflow and runs the steps of one job in order, the way the
GitHub runner does. A change to a `run:` step on GitHub is therefore picked up by GitLab without editing
anything here. What needs attention when the GitHub workflows change:
  - a new job (add a GitLab job in .gitlab-ci.yml),
  - a new `uses:` action (add a handler below: an unknown action fails the job on purpose),
  - a new `${{ }}` function or context (an unknown one fails the job on purpose).

    python3 ci/gitlab/gh_steps.py WORKFLOW JOB [--matrix KEY=VALUE]... [--skip-step REGEX]...
        [--skip-uses REGEX]... [--replace-step 'NAME=COMMAND']... [--only-step REGEX]... [--dry-run]

What is emulated (GitLab job semantics in brackets):
  run steps          bash/sh/pwsh/powershell/cmd/python/"msys2 {0}" shells, if:, env:, working-directory:,
                     continue-on-error:, timeout-minutes:, $GITHUB_ENV/$GITHUB_PATH/$GITHUB_OUTPUT/$GITHUB_STEP_SUMMARY
  expressions        ${{ }} with github/env/matrix/inputs/secrets/steps/needs/runner, ==, !=, &&, ||, !,
                     always/success/failure/cancelled/startsWith/endsWith/contains/hashFiles
  secrets.X          the GitLab CI/CD variable X (empty when unset, as for a fork on GitHub)
  inputs.X           workflow_dispatch inputs: the variable GH_INPUT_X (X upper-cased, - -> _)
  needs.J.outputs.O  the variable GH_NEEDS_J_O (from the release:version job's dotenv report)
  github.run_number  WWHD_RUN_NUMBER, else 100000 + CI_PIPELINE_IID (Android versionCode: above every
                     GitHub run number, so a GitLab APK installs over a GitHub one)
  actions/cache*     entries under .gitlab-ci-cache/ (kept between pipelines by the job's GitLab `cache:`),
                     same keys and restore-keys prefixes as on GitHub
  upload/download-artifact  files under .gitlab-ci-artifacts/<name>/ (the job's GitLab `artifacts:`)
  local composite actions (./.github/actions/*) run step by step like a workflow job
No secret value is ever printed by this script.
"""
import argparse
import fnmatch
import glob
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import urllib.request

try:
    import yaml
except ImportError:  # pragma: no cover
    sys.exit('gh_steps.py needs PyYAML (apt: python3-yaml, pip: pyyaml)')

WINDOWS = os.name == 'nt'
# GitHub steps that need a GitLab-specific command (applied in every job; --replace-step adds more)
REPLACEMENTS = {
    'Fetch pinned optional shader template':
        ('python' if WINDOWS else 'python3') + ' ci/gitlab/fetch_shader_template.py',
}
OS_NAME = 'Windows' if WINDOWS else ('macOS' if sys.platform == 'darwin' else 'Linux')


def log(msg):
    print(msg, flush=True)


def section(title, start=True, _n=[0]):
    """GitLab collapsible log section (plain text elsewhere)."""
    if not os.environ.get('GITLAB_CI'):
        if start:
            log('== ' + title)
        return
    if start:
        _n[0] += 1
        print(f'\x1b[0Ksection_start:{int(time.time())}:step{_n[0]}\r\x1b[0K\x1b[36;1m{title}\x1b[0m', flush=True)
    else:
        print(f'\x1b[0Ksection_end:{int(time.time())}:step{_n[0]}\r\x1b[0K', flush=True)


# --------------------------------------------------------------------------------------------------------
# ${{ }} expressions

class ExprError(Exception):
    pass


TOKEN = re.compile(r"""\s*(?:
    (?P<str>'(?:[^']|'')*')
  | (?P<num>-?\d+(?:\.\d+)?)
  | (?P<op>==|!=|<=|>=|&&|\|\||[!<>(),\[\]])
  | (?P<id>[A-Za-z_][A-Za-z0-9_\-]*(?:\.(?:[A-Za-z_*][A-Za-z0-9_\-]*))*)
)""", re.X)


def tokenize(text):
    out, pos = [], 0
    while pos < len(text):
        if text[pos:].strip() == '':
            break
        m = TOKEN.match(text, pos)
        if not m:
            raise ExprError(f'cannot parse expression near {text[pos:]!r}')
        pos = m.end()
        kind = m.lastgroup
        out.append((kind, m.group(kind)))
    return out


def truthy(v):
    return not (v is None or v is False or v == '' or v == 0)


def as_text(v):
    if v is None:
        return ''
    if v is True:
        return 'true'
    if v is False:
        return 'false'
    if isinstance(v, float) and v.is_integer():
        return str(int(v))
    if isinstance(v, (dict, list)):
        return json.dumps(v)
    return str(v)


def loose_eq(a, b):
    """GitHub's comparison: strings case-insensitively, mixed types as numbers."""
    if isinstance(a, str) and isinstance(b, str):
        return a.casefold() == b.casefold()
    if a is None and b is None:
        return True
    def num(v):
        if v is None:
            return 0.0
        if isinstance(v, bool):
            return 1.0 if v else 0.0
        if isinstance(v, (int, float)):
            return float(v)
        try:
            return float(v) if v.strip() else 0.0
        except (ValueError, AttributeError):
            return float('nan')
    return num(a) == num(b)


class Evaluator:
    def __init__(self, ctx, functions):
        self.ctx = ctx
        self.functions = functions

    def eval(self, text):
        self.toks = tokenize(text)
        self.i = 0
        v = self.or_()
        if self.i != len(self.toks):
            raise ExprError(f'unexpected {self.toks[self.i][1]!r} in {text!r}')
        return v

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else (None, None)

    def take(self, value=None):
        tok = self.peek()
        if value is not None and tok[1] != value:
            raise ExprError(f'expected {value!r}, got {tok[1]!r}')
        self.i += 1
        return tok

    def or_(self):
        v = self.and_()
        while self.peek()[1] == '||':
            self.take()
            r = self.and_()
            v = v if truthy(v) else r
        return v

    def and_(self):
        v = self.cmp()
        while self.peek()[1] == '&&':
            self.take()
            r = self.cmp()
            v = r if truthy(v) else v
        return v

    def cmp(self):
        v = self.unary()
        while self.peek()[1] in ('==', '!=', '<', '>', '<=', '>='):
            op = self.take()[1]
            r = self.unary()
            if op == '==':
                v = loose_eq(v, r)
            elif op == '!=':
                v = not loose_eq(v, r)
            else:
                a, b = float(as_text(v) or 0), float(as_text(r) or 0)
                v = {'<': a < b, '>': a > b, '<=': a <= b, '>=': a >= b}[op]
        return v

    def unary(self):
        if self.peek()[1] == '!':
            self.take()
            return not truthy(self.unary())
        return self.primary()

    def primary(self):
        kind, val = self.take()
        if kind == 'str':
            return val[1:-1].replace("''", "'")
        if kind == 'num':
            return float(val)
        if val == '(':
            v = self.or_()
            self.take(')')
            return v
        if kind != 'id':
            raise ExprError(f'unexpected {val!r}')
        if val in ('true', 'false'):
            return val == 'true'
        if val == 'null':
            return None
        if self.peek()[1] == '(':
            self.take()
            args = []
            while self.peek()[1] != ')':
                args.append(self.or_())
                if self.peek()[1] == ',':
                    self.take()
            self.take(')')
            fn = self.functions.get(val.lower())
            if fn is None:
                raise ExprError(f'function {val}() has no GitLab emulation (ci/gitlab/gh_steps.py)')
            return fn(*args)
        v = self.lookup(val.split('.'))
        while self.peek()[1] == '[':
            self.take()
            key = self.or_()
            self.take(']')
            v = v.get(as_text(key)) if isinstance(v, dict) else None
        return v

    def lookup(self, parts):
        root = parts[0]
        if root not in self.ctx:
            raise ExprError(f'context {root!r} has no GitLab emulation (ci/gitlab/gh_steps.py)')
        v = self.ctx[root]
        for p in parts[1:]:
            if callable(v):
                v = v(p)
            elif isinstance(v, dict):
                v = v.get(p)
            else:
                return None
        return v() if callable(v) and len(parts) == 1 else v


EXPR = re.compile(r'\$\{\{(.*?)\}\}', re.S)


# --------------------------------------------------------------------------------------------------------
# the runner

def var_name(*parts):
    return '_'.join(re.sub(r'[^A-Za-z0-9]', '_', p).upper() for p in parts)


def hash_files(workspace, *patterns):
    files = set()
    for pat in patterns:
        for f in glob.glob(str(workspace / pat), recursive=True):
            if os.path.isfile(f):
                files.add(os.path.normpath(f))
    if not files:
        return ''
    h = hashlib.sha256()
    for f in sorted(files):
        h.update(hashlib.sha256(Path(f).read_bytes()).digest())
    return h.hexdigest()


def read_env_file(path):
    """$GITHUB_ENV / $GITHUB_OUTPUT: NAME=value lines and NAME<<DELIM heredocs (UTF-8 or Windows
    PowerShell 5's UTF-16)."""
    if not path.exists():
        return {}
    raw = path.read_bytes()
    if raw.startswith((b'\xff\xfe', b'\xfe\xff')):
        text = raw.decode('utf-16')
    else:
        text = raw.decode('utf-8-sig')
    lines = text.replace('\r\n', '\n').split('\n')
    out, i = {}, 0
    while i < len(lines):
        line = lines[i]
        i += 1
        if not line.strip():
            continue
        m = re.match(r'^([^=<]+)<<(.+)$', line)
        if m:
            name, delim, body = m.group(1), m.group(2), []
            while i < len(lines) and lines[i] != delim:
                body.append(lines[i])
                i += 1
            i += 1
            out[name] = '\n'.join(body)
        elif '=' in line:
            k, v = line.split('=', 1)
            out[k] = v
    return out


class StepFailed(Exception):
    pass


class Job:
    def __init__(self, args):
        self.args = args
        self.workspace = Path(os.environ.get('CI_PROJECT_DIR') or os.getcwd()).resolve()
        self.cache_root = Path(os.environ.get('WWHD_CACHE_DIR') or self.workspace / '.gitlab-ci-cache')
        self.artifact_root = Path(os.environ.get('WWHD_ARTIFACT_DIR') or self.workspace / '.gitlab-ci-artifacts')
        self.temp = Path(tempfile.mkdtemp(prefix='gh-runner-temp-'))
        self.dry = args.dry_run
        self.failed = False
        self.post = []  # (description, callable) run at the end, like GitHub post steps
        self.msystem = 'MSYS'
        self.msys2_root = Path(os.environ.get('WWHD_MSYS2_ROOT', r'C:\msys64'))
        wf_path = self.workspace / args.workflow if not Path(args.workflow).is_absolute() else Path(args.workflow)
        self.workflow = yaml.safe_load(wf_path.read_text(encoding='utf-8'))
        self.wf_name = self.workflow.get('name', wf_path.stem)
        jobs = self.workflow.get('jobs') or {}
        if args.job not in jobs:
            sys.exit(f'{args.workflow}: no job {args.job!r} (jobs: {", ".join(jobs)})')
        self.job = jobs[args.job]
        self.matrix = self.pick_matrix(self.job.get('strategy', {}).get('matrix'), dict(args.matrix))
        self.extra_path = []
        self.env_added = {}
        sha = os.environ.get('CI_COMMIT_SHA') or self.git('rev-parse', 'HEAD') or ''
        tag = os.environ.get('CI_COMMIT_TAG', '')
        branch = os.environ.get('CI_COMMIT_BRANCH') or os.environ.get('CI_COMMIT_REF_NAME') or ''
        ref = f'refs/tags/{tag}' if tag else f'refs/heads/{branch}'
        run_number = os.environ.get('WWHD_RUN_NUMBER') or str(100000 + int(os.environ.get('CI_PIPELINE_IID') or 0))
        self.github = {
            'sha': sha, 'ref': ref, 'ref_name': tag or branch, 'workspace': str(self.workspace),
            # GitLab pipelines here are started by hand; "push" keeps the GitHub cache-save conditions
            # (pushes to devel/main) meaningful
            'event_name': os.environ.get('WWHD_GH_EVENT', 'push'),
            'run_number': run_number, 'run_id': os.environ.get('CI_PIPELINE_ID', '0'),
            'repository': os.environ.get('CI_PROJECT_PATH', ''), 'workflow': self.wf_name,
            'server_url': os.environ.get('CI_SERVER_URL', ''), 'token': '', 'event': {},
            'job': args.job, 'actor': os.environ.get('GITLAB_USER_LOGIN', ''),
        }
        self.base_env = {
            'GITHUB_WORKSPACE': str(self.workspace), 'GITHUB_SHA': sha, 'GITHUB_REF': ref,
            'GITHUB_REF_NAME': tag or branch, 'GITHUB_EVENT_NAME': self.github['event_name'],
            'GITHUB_RUN_NUMBER': run_number, 'GITHUB_WORKFLOW': self.wf_name, 'GITHUB_JOB': args.job,
            'RUNNER_TEMP': str(self.temp), 'RUNNER_OS': OS_NAME,
            'RUNNER_ARCH': 'ARM64' if platform.machine().lower() in ('arm64', 'aarch64') else 'X64',
            'ImageOS': os.environ.get('WWHD_IMAGE_OS', f'gitlab-{OS_NAME.lower()}'),
        }
        self.set_submodule_mirrors()

    # ---- helpers
    def git(self, *a):
        try:
            return subprocess.run(['git', '-C', str(self.workspace), *a], check=True,
                                  stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, text=True).stdout.strip()
        except (OSError, subprocess.CalledProcessError):
            return None

    @staticmethod
    def pick_matrix(spec, given):
        if not spec:
            return given
        if 'include' in spec and not any(k for k in spec if k != 'include'):
            for entry in spec['include']:
                if all(as_text(entry.get(k)) == v for k, v in given.items()):
                    return {k: as_text(v) for k, v in entry.items()}
            sys.exit(f'no matrix include entry matches {given}')
        for k, values in spec.items():
            if k in ('include', 'exclude'):
                continue
            if k not in given:
                sys.exit(f'matrix key {k!r} needs --matrix {k}=... ({values})')
            if given[k] not in [as_text(v) for v in values]:
                sys.exit(f'matrix {k}={given[k]!r} is not one of {values}')
        return given

    def mirror_url(self, repo):
        override = os.environ.get('WWHD_MIRROR_' + var_name(repo))
        return override or f'https://github.com/{repo}.git'

    def set_submodule_mirrors(self):
        """WWHD_MIRROR_<OWNER>_<REPO> also redirects submodules (e.g. decomp) once GitHub is gone."""
        gm = self.workspace / '.gitmodules'
        if not gm.exists() or self.dry:
            return
        for name, url in re.findall(r'\[submodule "([^"]+)"\][^\[]*?url\s*=\s*(\S+)', gm.read_text()):
            m = re.match(r'https://github\.com/([^/]+/[^/.]+)(?:\.git)?$', url)
            if m and os.environ.get('WWHD_MIRROR_' + var_name(m.group(1))):
                self.git('config', f'submodule.{name}.url', self.mirror_url(m.group(1)))
                log(f'submodule {name}: {self.mirror_url(m.group(1))}')

    def git_auth_env(self):
        """Read access to other projects on this GitLab with the job token (their CI/CD job token
        allowlist must include this project); the token never appears in a command line."""
        server, token = os.environ.get('CI_SERVER_URL'), os.environ.get('CI_JOB_TOKEN')
        if not (server and token):
            return {}
        import base64
        basic = base64.b64encode(f'gitlab-ci-token:{token}'.encode()).decode()
        return {'GIT_CONFIG_COUNT': '1', 'GIT_CONFIG_KEY_0': f'http.{server}/.extraHeader',
                'GIT_CONFIG_VALUE_0': f'Authorization: Basic {basic}'}

    # ---- contexts
    def make_eval(self, scope):
        def status_fn(kind):
            return lambda: {'always': True, 'success': not self.failed, 'failure': self.failed,
                            'cancelled': False}[kind]
        funcs = {
            'always': status_fn('always'), 'success': status_fn('success'),
            'failure': status_fn('failure'), 'cancelled': status_fn('cancelled'),
            'startswith': lambda a, b: as_text(a).casefold().startswith(as_text(b).casefold()),
            'endswith': lambda a, b: as_text(a).casefold().endswith(as_text(b).casefold()),
            'contains': lambda a, b: (b in a) if isinstance(a, list) else as_text(b).casefold() in as_text(a).casefold(),
            'hashfiles': lambda *p: hash_files(self.workspace, *p),
            'format': lambda f, *a: re.sub(r'\{(\d+)\}', lambda m: as_text(a[int(m.group(1))]), f),
            'tojson': lambda v: json.dumps(v), 'fromjson': lambda v: json.loads(v),
        }
        def env_var(prefix):
            return lambda name: os.environ.get(prefix + var_name(name))
        def needs(job):
            return {'outputs': lambda out: os.environ.get('GH_NEEDS_' + var_name(job, out)),
                    'result': 'success'}
        ctx = {
            'github': self.github,
            'env': scope['env'],
            'matrix': self.matrix,
            'inputs': scope.get('inputs') if scope.get('inputs') is not None else env_var('GH_INPUT_'),
            'secrets': lambda name: os.environ.get(name, ''),
            'steps': scope['steps'],
            'needs': needs,
            'runner': {'os': OS_NAME, 'temp': str(self.temp), 'arch': self.base_env['RUNNER_ARCH']},
            'job': {'status': 'failure' if self.failed else 'success'},
            'strategy': {'fail-fast': False, 'job-index': 0},
        }
        return Evaluator(ctx, funcs)

    def subst(self, value, scope):
        if value is None:
            return None
        if not isinstance(value, str):
            return value
        ev = self.make_eval(scope)
        m = re.fullmatch(r'\s*\$\{\{(.*?)\}\}\s*', value, re.S)
        if m and value.count('${{') == 1:
            return as_text(ev.eval(m.group(1)))
        return EXPR.sub(lambda mm: as_text(ev.eval(mm.group(1))), value)

    def cond(self, expr, scope, default='success()'):
        if expr is None:
            expr = default
        if isinstance(expr, bool):
            return expr
        expr = str(expr).strip()
        m = re.fullmatch(r'\$\{\{(.*)\}\}', expr, re.S)
        if m:
            expr = m.group(1)
        if not re.search(r'\b(always|success|failure|cancelled)\s*\(', expr):
            expr = f'success() && ({expr})'
        return truthy(self.make_eval(scope).eval(expr))

    def eval_env(self, mapping, scope):
        out = {}
        for k, v in (mapping or {}).items():
            scope_env = dict(scope['env'])
            scope_env.update(out)
            out[k] = as_text(self.subst(as_text(v) if not isinstance(v, str) else v,
                                        dict(scope, env=scope_env)))
        return out

    # ---- running
    def run(self):
        wf_env = self.eval_env(self.workflow.get('env'), {'env': {}, 'steps': {}})
        scope = {'env': wf_env, 'steps': {}}
        job_env = self.eval_env(self.job.get('env'), scope)
        env = dict(wf_env, **job_env)
        scope = {'env': env, 'steps': {}}
        log(f'GitHub workflow {self.args.workflow}, job {self.args.job}'
            + (f', matrix {self.matrix}' if self.matrix else '') + (' (dry run)' if self.dry else ''))
        if 'if' in self.job and not self.cond(self.job['if'], scope):
            log(f'job condition false ({self.job["if"]}): skipped, as on GitHub')
            return 0
        defaults = (self.job.get('defaults') or self.workflow.get('defaults') or {}).get('run', {})
        try:
            self.run_steps(self.job.get('steps', []), scope, defaults)
        finally:
            for desc, fn in self.post:
                section(f'Post: {desc}')
                try:
                    fn()
                except Exception as e:  # a post step never hides the job result
                    log(f'post step failed: {e}')
                section('', False)
            summary = self.temp / 'step-summary.md'
            if summary.exists() and summary.stat().st_size:
                log('---- step summary ----\n' + summary.read_text(errors='replace'))
            shutil.rmtree(self.temp, ignore_errors=True)
        return 1 if self.failed else 0

    def step_filtered(self, step, title):
        a = self.args
        if a.only_step and not any(re.search(r, title) for r in a.only_step):
            return 'not selected (--only-step)'
        for r in a.skip_step:
            if re.search(r, title):
                return f'skipped on GitLab (--skip-step {r!r})'
        for r in a.skip_uses:
            if 'uses' in step and re.search(r, step['uses']):
                return f'skipped on GitLab (--skip-uses {r!r})'
        return None

    def run_steps(self, steps, scope, defaults, nested=False):
        for idx, step in enumerate(steps):
            title = step.get('name') or (('Run ' + step['uses']) if 'uses' in step else
                                         'Run ' + step.get('run', '').strip().split('\n')[0][:60])
            sid = step.get('id') or f'__step{idx}'
            reason = None if nested else self.step_filtered(step, title)
            replace = None if nested else REPLACEMENTS.get(title)
            if not nested:
                for spec in self.args.replace_step:
                    name, cmd = spec.split('=', 1)
                    if name == title:
                        replace = cmd
            record = {'outputs': {}, 'outcome': 'skipped', 'conclusion': 'skipped'}
            scope['steps'][sid] = record
            if reason:
                log(f'-- {title}: {reason}')
                continue
            try:
                run_it = self.cond(step.get('if'), scope)
            except ExprError as e:
                if self.dry:
                    log(f'-- {title}: condition not evaluable in a dry run ({e}); shown')
                    run_it = True
                else:
                    raise
            if not run_it:
                why = f'condition false ({step["if"]})' if 'if' in step else 'an earlier step failed'
                log(f'-- {title}: skipped, {why}')
                continue
            section(title)
            # precedence as on GitHub: step env > $GITHUB_ENV > job env > workflow env
            step_env = dict(scope['env'])
            step_env.update(self.env_added)
            step_env.update(self.eval_env(step.get('env'), dict(scope, env=step_env)))
            try:
                if replace is not None:
                    log(f'(GitLab replacement: {replace})')
                    outputs = self.run_script(dict(step, run=replace), step_env, scope, defaults)
                elif 'run' in step:
                    outputs = self.run_script(step, step_env, scope, defaults)
                else:
                    outputs = self.run_action(step, step_env, scope)
                record.update(outputs=outputs or {}, outcome='success', conclusion='success')
            except (StepFailed, ExprError, subprocess.TimeoutExpired, OSError) as e:
                record['outcome'] = 'failure'
                if step.get('continue-on-error') in (True, 'true'):
                    record['conclusion'] = 'success'
                    log(f'step failed ({e}); continue-on-error')
                else:
                    record['conclusion'] = 'failure'
                    log(f'ERROR: step "{title}" failed: {e}')
                    self.failed = True
            finally:
                section('', False)

    def shell_command(self, shell, script_dir, body):
        shell = (shell or ('pwsh' if WINDOWS else 'bash')).strip()
        def write(ext, text, crlf=False):
            p = script_dir / f'step{ext}'
            data = text.replace('\r\n', '\n')
            if crlf:
                data = data.replace('\n', '\r\n')
            p.write_bytes(data.encode('utf-8'))
            return p
        env = {}
        if shell == 'bash':
            p = write('.sh', body)
            bash = os.environ.get('WWHD_BASH') or (r'C:\Program Files\Git\bin\bash.exe' if WINDOWS else 'bash')
            return [bash, '--noprofile', '--norc', '-eo', 'pipefail', p.as_posix()], env
        if shell == 'sh':
            return ['sh', '-e', write('.sh', body).as_posix()], env
        if shell in ('pwsh', 'powershell'):
            exe = shell
            if shell == 'pwsh' and not shutil.which('pwsh'):
                log('notice: no pwsh (PowerShell 7) on this runner; using Windows PowerShell')
                exe = 'powershell'
            p = write('.ps1', "$ErrorActionPreference = 'stop'\n" + body +
                      "\nif ((Test-Path -LiteralPath variable:\\LASTEXITCODE)) { exit $LASTEXITCODE }\n", crlf=WINDOWS)
            return [exe, '-NoProfile', '-NonInteractive', '-command', f". '{p}'"], env
        if shell == 'cmd':
            p = write('.cmd', body, crlf=True)
            return ['cmd', '/D', '/E:ON', '/V:OFF', '/S', '/C', f'CALL "{p}"'], env
        if shell == 'python':
            return [sys.executable, str(write('.py', body))], env
        if shell == 'msys2 {0}':
            p = write('.sh', body)
            env = {'MSYSTEM': self.msystem, 'CHERE_INVOKING': '1', 'MSYS2_PATH_TYPE': 'minimal'}
            return [str(self.msys2_root / 'usr' / 'bin' / 'bash.exe'), '-leo', 'pipefail', p.as_posix()], env
        if '{0}' in shell:
            p = write('.sh', body)
            return [w.replace('{0}', str(p)) for w in shlex.split(shell)], env
        raise StepFailed(f'shell {shell!r} has no GitLab emulation')

    def proc_env(self, step_env):
        env = os.environ.copy()
        env.update(self.base_env)
        env.update(step_env)
        if self.extra_path:
            env['PATH'] = os.pathsep.join(self.extra_path[::-1] + [env.get('PATH', '')])
        return env

    def run_script(self, step, step_env, scope, defaults):
        body = self.subst(step['run'], dict(scope, env=step_env))
        shell = step.get('shell') or defaults.get('shell')
        wd = step.get('working-directory') or defaults.get('working-directory')
        cwd = self.workspace / self.subst(wd, scope) if wd else self.workspace
        if self.dry:
            log(f'[{shell or "default shell"}] in {cwd.relative_to(self.workspace) if wd else "."}\n{body}')
            if self.args.render_dir:
                d = Path(self.args.render_dir)
                d.mkdir(parents=True, exist_ok=True)
                n = len(list(d.iterdir()))
                ext = {'pwsh': 'ps1', 'powershell': 'ps1', 'cmd': 'cmd', 'python': 'py'}.get(shell or '', 'sh')
                (d / f'{n:03d}.{ext}').write_text(body)
            return {}
        sdir = Path(tempfile.mkdtemp(dir=self.temp, prefix='step-'))
        files = {n: sdir / n for n in ('env', 'path', 'output')}
        for f in files.values():
            f.touch()
        summary = self.temp / 'step-summary.md'
        summary.touch()
        cmd, shell_env = self.shell_command(shell, sdir, body)
        env = self.proc_env(step_env)
        env.update(shell_env)
        env.update({'GITHUB_ENV': str(files['env']), 'GITHUB_PATH': str(files['path']),
                    'GITHUB_OUTPUT': str(files['output']), 'GITHUB_STEP_SUMMARY': str(summary)})
        timeout = step.get('timeout-minutes')
        timeout = float(timeout) * 60 if timeout else None
        kwargs = {'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP} if WINDOWS else {'start_new_session': True}
        p = subprocess.Popen(cmd, cwd=cwd, env=env, **kwargs)
        try:
            rc = p.wait(timeout=timeout)
        except subprocess.TimeoutExpired:
            if WINDOWS:
                subprocess.run(['taskkill', '/T', '/F', '/PID', str(p.pid)], capture_output=True)
            else:
                os.killpg(p.pid, signal.SIGKILL)
            p.wait()
            raise StepFailed(f'timed out after {step.get("timeout-minutes")} min')
        if rc != 0:
            raise StepFailed(f'exit code {rc}')
        self.env_added.update(read_env_file(files['env']))
        for line in files['path'].read_text(errors='replace').splitlines():
            if line.strip():
                self.extra_path.append(line.strip())
        return read_env_file(files['output'])

    # ---- uses:
    def run_action(self, step, step_env, scope):
        uses = step['uses']
        with_ = {k: self.subst(as_text(v) if not isinstance(v, str) else v, dict(scope, env=step_env))
                 for k, v in (step.get('with') or {}).items()}
        name = uses.split('@')[0]
        if name.startswith('./'):
            return self.composite(name, with_, step_env)
        handler = {
            'actions/checkout': self.a_checkout,
            'actions/cache': lambda w, e: self.a_cache(w, restore=True, save='post'),
            'actions/cache/restore': lambda w, e: self.a_cache(w, restore=True, save=None),
            'actions/cache/save': lambda w, e: self.a_cache(w, restore=False, save='now'),
            'actions/upload-artifact': self.a_upload,
            'actions/download-artifact': self.a_download,
            'actions/setup-java': self.a_setup_java,
            'actions/setup-python': self.a_setup_python,
            'gradle/actions/wrapper-validation': self.a_wrapper_validation,
            'msys2/setup-msys2': self.a_msys2,
            'reactivecircus/android-emulator-runner': self.a_emulator,
        }.get(name)
        if handler is None:
            raise StepFailed(f'action {uses} has no GitLab handler: add one to ci/gitlab/gh_steps.py')
        if self.dry:
            log(f'[action {uses}] with {json.dumps(with_)}')
            return {}
        return handler(with_, step_env) or {}

    def composite(self, path, with_, step_env):
        action_file = self.workspace / path / 'action.yml'
        if not action_file.exists():
            action_file = self.workspace / path / 'action.yaml'
        action = yaml.safe_load(action_file.read_text(encoding='utf-8'))
        inputs = {}
        for k, spec in (action.get('inputs') or {}).items():
            inputs[k] = with_.get(k, as_text((spec or {}).get('default')))
        scope = {'env': dict(step_env), 'steps': {}, 'inputs': inputs}
        runs = action.get('runs', {})
        if runs.get('using') != 'composite':
            raise StepFailed(f'{path}: only composite local actions are supported')
        before = self.failed
        self.run_steps(runs.get('steps', []), scope, {}, nested=True)
        if self.failed and not before:
            raise StepFailed(f'{path} failed')
        outputs = {}
        for k, spec in (action.get('outputs') or {}).items():
            outputs[k] = as_text(self.subst((spec or {}).get('value', ''), scope))
        return outputs

    def a_checkout(self, w, env):
        repo = w.get('repository')
        if not repo:
            # GitLab has checked out this commit already
            if w.get('submodules') in ('true', 'recursive'):
                self.set_submodule_mirrors()
                rec = ['--recursive'] if w.get('submodules') == 'recursive' else []
                subprocess.run(['git', 'submodule', 'update', '--init', *rec], cwd=self.workspace, check=True,
                               env=dict(os.environ, **self.git_auth_env()))
            return {}
        dest = self.workspace / (w.get('path') or '.')
        url = self.mirror_url(repo)
        ref = w.get('ref') or 'HEAD'
        dest.mkdir(parents=True, exist_ok=True)
        env = dict(os.environ, GIT_TERMINAL_PROMPT='0', **self.git_auth_env())
        log(f'checkout {url} {ref} -> {dest.relative_to(self.workspace)}')
        # origin names the GitHub repository, as actions/checkout leaves it (tools check it); the objects may
        # come from a mirror (WWHD_MIRROR_*), and a pinned ref is a commit, so the content is the same
        subprocess.run(['git', '-C', str(dest), 'init', '-q'], check=True, env=env)
        if subprocess.run(['git', '-C', str(dest), 'remote', 'get-url', 'origin'], capture_output=True).returncode:
            subprocess.run(['git', '-C', str(dest), 'remote', 'add', 'origin', f'https://github.com/{repo}'], check=True)
        else:
            subprocess.run(['git', '-C', str(dest), 'remote', 'set-url', 'origin', f'https://github.com/{repo}'], check=True)
        for args in (['fetch', '-q', '--depth=1', url, ref], ['checkout', '-q', '--detach', 'FETCH_HEAD']):
            subprocess.run(['git', '-C', str(dest), *args], check=True, env=env)
        return {}

    # cache entries: <cache_root>/<id>/meta.json + data/<n>/ (one folder per path)
    def cache_paths(self, w):
        out = []
        for line in (w.get('path') or '').splitlines():
            line = line.strip()
            if line:
                p = Path(os.path.expanduser(line))
                out.append(p if p.is_absolute() else self.workspace / p)
        return out

    def cache_entries(self):
        if not self.cache_root.exists():
            return []
        out = []
        for meta in self.cache_root.glob('*/meta.json'):
            try:
                out.append((meta.parent, json.loads(meta.read_text())))
            except (OSError, ValueError):
                pass
        return sorted(out, key=lambda e: e[1].get('time', 0), reverse=True)

    @staticmethod
    def copy_tree(src, dst):
        if dst.is_symlink() or dst.is_file():
            dst.unlink()
        elif dst.exists():
            shutil.rmtree(dst)
        dst.parent.mkdir(parents=True, exist_ok=True)
        if src.is_dir():
            def link_or_copy(a, b):
                try:
                    os.link(a, b)
                except OSError:
                    shutil.copy2(a, b)
            shutil.copytree(src, dst, symlinks=True, copy_function=link_or_copy)
        else:
            shutil.copy2(src, dst)

    def a_cache(self, w, restore, save):
        key = w.get('key', '')
        paths = self.cache_paths(w)
        rel = sorted(str(p) for p in paths)
        out = {'cache-hit': 'false'}
        hit = None
        if restore:
            entries = self.cache_entries()
            hit = next((e for e in entries if e[1].get('key') == key and e[1].get('paths') == rel), None)
            exact = hit is not None
            if not hit:
                for prefix in (w.get('restore-keys') or '').splitlines():
                    prefix = prefix.strip()
                    hit = next((e for e in entries if prefix and e[1].get('key', '').startswith(prefix)
                                and e[1].get('paths') == rel), None)
                    if hit:
                        break
            if hit:
                if w.get('lookup-only') != 'true':
                    for i, p in enumerate(paths):
                        src = hit[0] / 'data' / str(i)
                        if src.exists():
                            self.copy_tree(src, p)
                log(f'cache {"hit" if exact else "restored from prefix"}: {hit[1]["key"]}')
                out['cache-hit'] = 'true' if exact else 'false'
                out['cache-matched-key'] = hit[1]['key']
            else:
                log(f'cache miss: {key}')
        def do_save():
            if save == 'post' and (self.failed or out['cache-hit'] == 'true'):
                return
            missing = [p for p in paths if not p.exists()]
            if missing:
                log(f'cache not saved, missing: {", ".join(map(str, missing))}')
                return
            for d, meta in self.cache_entries():
                if meta.get('paths') == rel:   # one entry per path set: older keys are replaced
                    shutil.rmtree(d, ignore_errors=True)
            d = self.cache_root / hashlib.sha256(key.encode()).hexdigest()[:20]
            for i, p in enumerate(paths):
                self.copy_tree(p, d / 'data' / str(i))
            (d / 'meta.json').write_text(json.dumps({'key': key, 'paths': rel, 'time': time.time()}))
            log(f'cache saved: {key}')
        if save == 'now':
            do_save()
        elif save == 'post':
            self.post.append((f'save cache {key}', do_save))
        return out

    def a_upload(self, w, env):
        name = w.get('name') or 'artifact'
        patterns = [l.strip() for l in (w.get('path') or '').splitlines() if l.strip()]
        include, exclude = [], []
        for pat in patterns:
            (exclude if pat.startswith('!') else include).append(pat.lstrip('!'))
        files = []
        for pat in include:
            p = Path(os.path.expanduser(pat))
            pat_abs = str(p if p.is_absolute() else self.workspace / p)
            matches = glob.glob(pat_abs, recursive=True)
            for m in matches:
                mp = Path(m)
                if mp.is_dir():
                    files += [f for f in mp.rglob('*') if f.is_file()]
                elif mp.is_file():
                    files.append(mp)
        files = sorted({f.resolve() for f in files
                        if not any(fnmatch.fnmatch(str(f), str(self.workspace / x)) for x in exclude)})
        if not files:
            mode = w.get('if-no-files-found', 'warn')
            if mode == 'error':
                raise StepFailed(f'artifact {name}: no files match {patterns}')
            log(f'{"warning: " if mode == "warn" else ""}artifact {name}: no files')
            return {}
        root = Path(os.path.commonpath([str(f.parent) for f in files]))
        dest = self.artifact_root / name
        for f in files:
            target = dest / f.relative_to(root)
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(f, target)
        log(f'artifact {name}: {len(files)} file(s) -> {dest.relative_to(self.workspace)} '
            '(kept by the GitLab job artifacts)')
        return {}

    def a_download(self, w, env):
        dest = self.workspace / (w.get('path') or '.')
        if w.get('name'):
            sources = [self.artifact_root / w['name']]
            merge = True
        else:
            sources = [d for d in sorted(self.artifact_root.glob('*'))
                       if d.is_dir() and fnmatch.fnmatch(d.name, w.get('pattern') or '*')]
            merge = w.get('merge-multiple') == 'true'
        if not any(s.is_dir() for s in sources):
            raise StepFailed(f'no artifacts {w.get("name") or w.get("pattern")} from earlier jobs '
                             '(GitLab needs:/dependencies)')
        for s in sources:
            if not s.is_dir():
                continue
            target = dest if merge else dest / s.name
            shutil.copytree(s, target, dirs_exist_ok=True)
            log(f'artifact {s.name} -> {target.relative_to(self.workspace)}')
        return {}

    def a_setup_java(self, w, env):
        want = int(str(w.get('java-version', '17')).split('.')[0])
        try:
            out = subprocess.run(['java', '-version'], capture_output=True, text=True).stderr
        except OSError:
            raise StepFailed('no java on this runner (the GitLab job image must provide a JDK)')
        m = re.search(r'version "(\d+)', out)
        have = int(m.group(1)) if m else 0
        log(f'java {have} (workflow asks for {w.get("distribution", "")} {want})')
        if have != want:
            raise StepFailed(f'java {want} needed, the runner has {have}')
        return {}

    def a_setup_python(self, w, env):
        want = str(w.get('python-version', ''))
        exe = os.environ.get('WWHD_SETUP_PYTHON')  # set by ci/gitlab/windows_bootstrap.ps1
        if exe:
            self.extra_path += [str(Path(exe).parent / 'Scripts'), str(Path(exe).parent)]
        probe = exe or ('python' if WINDOWS else 'python3')
        ver = subprocess.run([probe, '-c', 'import sys; print("%d.%d.%d" % sys.version_info[:3])'],
                             capture_output=True, text=True).stdout.strip()
        log(f'python {ver} from the runner (workflow asks for {want})')
        if want and not ver.startswith(want):
            log(f'notice: the runner\'s Python is {ver}, the GitHub job uses {want}')
        return {'python-version': ver}

    def a_wrapper_validation(self, w, env):
        jars = [p for p in self.workspace.rglob('gradle-wrapper.jar') if '.gitlab-ci' not in str(p)]
        for jar in jars:
            props = jar.with_name('gradle-wrapper.properties')
            m = re.search(r'gradle-([0-9.]+)-(?:bin|all)\.zip', props.read_text()) if props.exists() else None
            if not m:
                raise StepFailed(f'{jar}: no Gradle version in {props}')
            url = f'https://services.gradle.org/distributions/gradle-{m.group(1)}-wrapper.jar.sha256'
            want = urllib.request.urlopen(url, timeout=60).read().decode().strip()
            have = hashlib.sha256(jar.read_bytes()).hexdigest()
            log(f'{jar.relative_to(self.workspace)}: {have} (Gradle {m.group(1)} publishes {want})')
            if have != want:
                raise StepFailed(f'{jar} is not the official Gradle {m.group(1)} wrapper')
        if not jars:
            raise StepFailed('no gradle-wrapper.jar found')
        return {}

    def a_msys2(self, w, env):
        if not WINDOWS:
            raise StepFailed('msys2/setup-msys2 runs on Windows only')
        self.msystem = w.get('msystem', 'MSYS')
        root = self.msys2_root
        bash = root / 'usr' / 'bin' / 'bash.exe'
        if not bash.exists():
            raise StepFailed(f'no MSYS2 at {root} (ci/gitlab/windows_bootstrap.ps1 installs it)')
        def sh(cmd):
            subprocess.run([str(bash), '-lc', cmd], check=True,
                           env=dict(os.environ, MSYSTEM=self.msystem, CHERE_INVOKING='1'))
        if w.get('update') == 'true':
            sh('pacman -Syuu --noconfirm --disable-download-timeout || true')
            sh('pacman -Syuu --noconfirm --disable-download-timeout')
        pkgs = (w.get('install') or '').split()
        if pkgs:
            sh('pacman -S --needed --noconfirm --disable-download-timeout ' + ' '.join(pkgs))
        return {'msys2-location': str(root)}

    def a_emulator(self, w, env):
        if os.environ.get('WWHD_ANDROID_EMULATOR') != '1':
            raise StepFailed('Android emulator steps need a self-hosted Linux runner with KVM '
                             '(tag wwhd-linux-kvm, variable WWHD_ANDROID_EMULATOR=1)')
        script = self.workspace / 'ci' / 'gitlab' / 'android_emulator.sh'
        args = [w.get('api-level', ''), w.get('arch', 'x86_64'), w.get('target', 'google_apis'),
                w.get('avd-name', 'test'), w.get('disk-size', ''), w.get('emulator-options', ''),
                w.get('script', '')]
        subprocess.run(['bash', str(script), *args], cwd=self.workspace, check=True, env=self.proc_env(env))
        return {}


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('workflow')
    ap.add_argument('job')
    ap.add_argument('--matrix', action='append', default=[], type=lambda s: tuple(s.split('=', 1)))
    ap.add_argument('--skip-step', action='append', default=[], metavar='REGEX')
    ap.add_argument('--skip-uses', action='append', default=[], metavar='REGEX',
                    help='skip the steps whose uses: matches')
    ap.add_argument('--only-step', action='append', default=[], metavar='REGEX')
    ap.add_argument('--replace-step', action='append', default=[], metavar='NAME=COMMAND')
    ap.add_argument('--dry-run', action='store_true', help='print the steps, run nothing')
    ap.add_argument('--render-dir', help='dry run: also write each run step to this folder')
    args = ap.parse_args()
    sys.exit(Job(args).run())


if __name__ == '__main__':
    main()
