#!/usr/bin/env python3
"""Offline check of .gitlab-ci.yml's rules: which pipeline sources start a pipeline and which jobs it has.

    python3 ci/gitlab/check_rules.py [.gitlab-ci.yml]

Evaluates `workflow:rules` and each job's `rules` (after `extends`) the way GitLab does for the expression
forms this file uses ($VAR, == / != "string", &&, ||, parentheses), for a set of scenarios. Fails when a
push or merge request (with or without WWHD_GITLAB_CI=1) would create a pipeline, or when an expected job
set is wrong. Needs PyYAML.
"""
import re
import sys

import yaml

SCENARIOS = [
    # (name, CI_PIPELINE_SOURCE, pipeline variables, expect a pipeline)
    ('mirror push of a branch', 'push', {}, False),
    ('mirror push of a tag', 'push', {'CI_COMMIT_TAG': 'v9.9.9'}, False),
    ('push with WWHD_GITLAB_CI=1 (project variable)', 'push', {'WWHD_GITLAB_CI': '1'}, False),
    ('merge request', 'merge_request_event', {}, False),
    ('merge request with WWHD_GITLAB_CI=1', 'merge_request_event', {'WWHD_GITLAB_CI': '1'}, False),
    ('external pull request', 'external_pull_request_event', {}, False),
    ('schedule', 'schedule', {}, False),
    ('API call', 'api', {}, False),
    ('parent pipeline', 'parent_pipeline', {}, False),
    ('Run pipeline button', 'web', {}, True),
    ('Run pipeline, WWHD_CI_AUTO=1', 'web', {'WWHD_CI_AUTO': '1'}, True),
    ('Run pipeline, release on a tag', 'web', {'WWHD_RELEASE': '1', 'CI_COMMIT_TAG': 'v9.9.9'}, True),
    ('Run pipeline, release without macOS, with Windows arm64', 'web',
     {'WWHD_RELEASE': '1', 'WWHD_SKIP_MACOS': '1', 'WWHD_WINDOWS_ARM64': '1'}, True),
    ('Run pipeline, emulator runner', 'web', {'WWHD_ANDROID_EMULATOR': '1'}, True),
    ('trigger token', 'trigger', {}, True),
    ('schedule with WWHD_GITLAB_CI=1', 'schedule', {'WWHD_GITLAB_CI': '1'}, True),
]

TOKEN = re.compile(r'\s*(?:(?P<var>\$[A-Za-z_][A-Za-z0-9_]*)|(?P<str>"[^"]*"|\'[^\']*\')|'
                   r'(?P<op>==|!=|&&|\|\||\(|\)|=~|!~)|(?P<null>null))')


def evaluate(expr, variables):
    toks, pos = [], 0
    while pos < len(expr.strip()):
        m = TOKEN.match(expr, pos)
        if not m:
            raise SystemExit(f'cannot evaluate rule expression {expr!r} near {expr[pos:]!r}')
        toks.append((m.lastgroup, m.group(m.lastgroup)))
        pos = m.end()
        if not expr[pos:].strip():
            break
    i = 0

    def value(tok):
        kind, text = tok
        if kind == 'var':
            return variables.get(text[1:])
        if kind == 'str':
            return text[1:-1]
        return None

    def primary():
        nonlocal i
        kind, text = toks[i]
        i += 1
        if text == '(':
            v = or_()
            i += 1
            return v
        left = value((kind, text))
        if i < len(toks) and toks[i][1] in ('==', '!='):
            op = toks[i][1]
            right = value(toks[i + 1])
            i += 2
            return (left == right) if op == '==' else (left != right)
        if i < len(toks) and toks[i][1] in ('=~', '!~'):
            raise SystemExit('regex rules are not supported by this checker')
        return bool(left)

    def and_():
        nonlocal i
        v = primary()
        while i < len(toks) and toks[i][1] == '&&':
            i += 1
            r = primary()
            v = v and r
        return v

    def or_():
        nonlocal i
        v = and_()
        while i < len(toks) and toks[i][1] == '||':
            i += 1
            r = and_()
            v = v or r
        return v

    return or_()


def first_match(rules, variables):
    for rule in rules:
        if 'if' not in rule or evaluate(rule['if'], variables):
            return rule
    return None


def merge(base, over):
    out = dict(base)
    for k, v in over.items():
        out[k] = merge(out[k], v) if isinstance(v, dict) and isinstance(out.get(k), dict) else v
    return out


def resolve(doc, name, seen=()):
    job = doc[name]
    ext = job.get('extends', [])
    ext = [ext] if isinstance(ext, str) else ext
    out = {}
    for e in ext:
        if e in seen:
            raise SystemExit(f'extends loop at {e}')
        out = merge(out, resolve(doc, e, seen + (e,)))
    return merge(out, {k: v for k, v in job.items() if k != 'extends'})


def main():
    path = sys.argv[1] if len(sys.argv) > 1 else '.gitlab-ci.yml'
    doc = yaml.safe_load(open(path, encoding='utf-8'))
    reserved = {'workflow', 'stages', 'variables', 'default', 'include', 'image', 'services', 'cache',
                'before_script', 'after_script'}
    jobs = {n: resolve(doc, n) for n in doc if n not in reserved and not n.startswith('.')}
    globals_ = {k: str(v if not isinstance(v, dict) else v.get('value', '')) for k, v in doc.get('variables', {}).items()}
    failures = 0
    for name, source, pvars, expect in SCENARIOS:
        variables = dict(globals_, CI_PIPELINE_SOURCE=source, CI_COMMIT_BRANCH='devel', **pvars)
        rule = first_match(doc['workflow']['rules'], variables)
        created = rule is not None and rule.get('when') != 'never'
        status = 'ok' if created == expect else 'WRONG'
        failures += status != 'ok'
        print(f'[{status}] {name}: {"pipeline" if created else "no pipeline"}')
        if not created:
            continue
        variables.update((rule or {}).get('variables', {}))
        listed = []
        for jname, job in jobs.items():
            jrule = first_match(job.get('rules', [{}]), variables) if 'rules' in job else {}
            if jrule is None or jrule.get('when') == 'never':
                continue
            when = jrule.get('when', job.get('when', 'on_success'))
            tags = job.get('tags', [])
            jvars = dict(variables)
            jvars.update(job.get('variables', {}))
            jvars.update(jrule.get('variables', {}))
            matrix_tags = [str(e['RUNNER_TAG']) for e in (job.get('parallel') or {}).get('matrix', [])
                           if 'RUNNER_TAG' in e]
            if matrix_tags:   # parallel:matrix sets the tag per instance
                jvars['RUNNER_TAG'] = '|'.join(matrix_tags)
            tags = [re.sub(r'\$(\w+)', lambda m: jvars.get(m.group(1), m.group(0)), t) for t in tags]
            listed.append(f'{jname} ({when}; {",".join(tags) or "-"})')
        print('      ' + '\n      '.join(listed))
    # every needs: target must be a job (or optional)
    for jname, job in jobs.items():
        for n in job.get('needs', []):
            target = n['job'] if isinstance(n, dict) else n
            if target not in jobs:
                print(f'[WRONG] {jname} needs unknown job {target}')
                failures += 1
    if failures:
        sys.exit(f'{failures} problem(s)')
    print('all rule checks passed')


if __name__ == '__main__':
    main()
