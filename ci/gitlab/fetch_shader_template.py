#!/usr/bin/env python3
"""GitLab stand-in for the "Fetch pinned optional shader template" step (tools/release/fetch_shader_template.py).

Same pin (tools/shadercache/template.ref), same checksum and guard checks; only the source differs: the
private GitLab project shader-template in this project's group/namespace ($CI_PROJECT_NAMESPACE/shader-template;
SHADER_TEMPLATE_REPOSITORY, an SSH URL, overrides it) instead of GitHub.

Access, in this order:
  SHADER_TEMPLATE_DEPLOY_KEY   a read-only deploy key of that project: a "File" CI/CD variable (the
                               variable holds the file's path), the key text itself, or the key base64-encoded
                               (one line, so GitLab can mask it)
  WWHD_SHADER_TEMPLATE_JOB_TOKEN=1   the job's CI_JOB_TOKEN over HTTPS (that project's CI/CD job token
                               allowlist must include this project)
Without either, the release is packaged without the optional shader preparation, as on GitHub, except
when the pipeline publishes a release or makes a development build (then the template is required).
"""
import base64
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools' / 'release'))
import fetch_shader_template as upstream  # noqa: E402

# gitlab.com's published SSH host key (docs.gitlab.com/user/gitlab_com/#ssh-known_hosts-entries); another GitLab
# server needs SHADER_TEMPLATE_HOST_KEY (its known_hosts line)
GITLAB_HOST_KEY = 'gitlab.com ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIAfuCHKVTjquxvt6CM6tdG4SLp1Btn/nOeHHE5UOzRdf\n'


def project():
    namespace = os.environ.get('CI_PROJECT_NAMESPACE')
    if not namespace:
        raise SystemExit('CI_PROJECT_NAMESPACE is not set (not a GitLab job): set SHADER_TEMPLATE_REPOSITORY')
    return f'{namespace}/shader-template'


def load_key():
    value = os.environ.get('SHADER_TEMPLATE_DEPLOY_KEY', '')
    if not value.strip():
        return ''
    if '\n' not in value and len(value) < 4096 and os.path.isfile(value):
        value = Path(value).read_text()
    if 'PRIVATE KEY' not in value:
        try:
            value = base64.b64decode(''.join(value.split()), validate=True).decode('ascii')
        except ValueError:
            raise SystemExit('SHADER_TEMPLATE_DEPLOY_KEY is neither an SSH private key nor its base64 form')
    return value


def required():
    return (os.environ.get('GH_NEEDS_VERSION_PUBLISH') == 'true' or
            os.environ.get('GH_NEEDS_VERSION_DEVEL') == 'true')


def fetch_with_job_token(destination):
    server = os.environ['CI_SERVER_URL']
    token = os.environ['CI_JOB_TOKEN']
    url = os.environ.get('SHADER_TEMPLATE_HTTPS_URL') or f'{server}/{project()}.git'
    pin = upstream.read_pin(ROOT / 'tools/shadercache/template.ref')
    basic = base64.b64encode(f'gitlab-ci-token:{token}'.encode()).decode()
    env = dict(os.environ, GIT_TERMINAL_PROMPT='0', GIT_CONFIG_COUNT='1',
               GIT_CONFIG_KEY_0=f'http.{server}/.extraHeader',
               GIT_CONFIG_VALUE_0=f'Authorization: Basic {basic}')  # never on a command line
    with tempfile.TemporaryDirectory() as temporary:
        def git(*args):
            return subprocess.run(['git', '-C', temporary, *args], env=env, check=True,
                                  stdout=subprocess.PIPE).stdout
        git('init', '--quiet')
        git('fetch', '--quiet', '--depth=1', url, pin['commit'])
        if git('rev-parse', 'FETCH_HEAD').decode().strip() != pin['commit']:
            raise ValueError('Shader template commit mismatch')
        data = git('show', 'FETCH_HEAD:template.bin')
    if hashlib.sha256(data).hexdigest() != pin['sha256']:
        raise ValueError('Shader template checksum mismatch')
    import guard
    problems = []
    guard.check_entry('tools/shadercache/template.bin', data, problems)
    if problems:
        raise ValueError('Shader template guard rejected payload: ' + '; '.join(problems))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(data)


def main():
    destination = ROOT / 'tools/shadercache/template.bin'
    key = load_key()
    # the upstream fetch() decides by GitHub's variables; none of them describe this pipeline
    for name in ('GITHUB_REPOSITORY', 'GITHUB_EVENT_NAME'):
        os.environ.pop(name, None)
    if key:
        host = os.environ.get('CI_SERVER_SHELL_SSH_HOST') or os.environ.get('CI_SERVER_HOST') or 'gitlab.com'
        upstream.REPOSITORY = (os.environ.get('SHADER_TEMPLATE_REPOSITORY')
                               or f'git@{host}:{project()}.git')
        host_key = os.environ.get('SHADER_TEMPLATE_HOST_KEY', '').strip()
        upstream.GITHUB_HOST_KEY = host_key + '\n' if host_key else GITLAB_HOST_KEY
        os.environ['SHADER_TEMPLATE_DEPLOY_KEY'] = key
        os.environ.setdefault('RUNNER_TEMP', tempfile.gettempdir())
        upstream.fetch()
    elif os.environ.get('WWHD_SHADER_TEMPLATE_JOB_TOKEN') == '1':
        fetch_with_job_token(destination)
    else:
        destination.unlink(missing_ok=True)
        if required():
            raise SystemExit('the shader template is required for a published release or development build: '
                             'set SHADER_TEMPLATE_DEPLOY_KEY or WWHD_SHADER_TEMPLATE_JOB_TOKEN=1 (docs/gitlab-ci.md)')
        print('notice: no shader template access; packaging without optional shader preparation')
        return
    print(f'shader template {upstream.read_pin(ROOT / "tools/shadercache/template.ref")["commit"]} fetched and verified')


if __name__ == '__main__':
    main()
