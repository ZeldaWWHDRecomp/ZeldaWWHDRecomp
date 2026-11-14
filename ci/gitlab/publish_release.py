#!/usr/bin/env python3
"""GitLab stand-in for release.yml's "Publish GitHub Release" and vt-notes steps.

    publish_release.py publish DIST       uploads DIST's release files to this project's generic package
                                          registry (package "wwhd", version = the release) and creates the
                                          GitLab release for the existing tag, notes from DIST/notes.md
    publish_release.py vt-notes REPORT    appends the VirusTotal section (tools/release/virustotal.py
                                          --render-notes) to the release and links the report

Uses the job's CI_JOB_TOKEN (the Releases and package APIs accept it); nothing secret is printed.
Variables: GH_NEEDS_VERSION_VERSION / GH_NEEDS_VERSION_PUBLISH (ci/gitlab/release_version.sh).
"""
import glob
import json
import os
from pathlib import Path
import subprocess
import sys
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
API = os.environ.get('CI_API_V4_URL', '')
PROJECT = os.environ.get('CI_PROJECT_ID', '')
PATTERNS = ('*.zip', '*.AppImage', '*.apk', 'SHA256SUMS.txt')


def call(method, path, data=None, body=None, content_type='application/json'):
    req = urllib.request.Request(f'{API}/projects/{PROJECT}{path}', method=method,
                                 headers={'JOB-TOKEN': os.environ['CI_JOB_TOKEN']})
    if data is not None:
        body = json.dumps(data).encode()
    if body is not None:
        req.add_header('Content-Type', content_type)
        req.data = body
    try:
        with urllib.request.urlopen(req, timeout=600) as r:
            text = r.read().decode()
            return json.loads(text) if text.strip().startswith(('{', '[')) else text
    except urllib.error.HTTPError as e:
        raise SystemExit(f'{method} {path}: HTTP {e.code} {e.read().decode(errors="replace")[:500]}')


def upload(version, path):
    name = path.name
    url_path = f'/packages/generic/wwhd/{urllib.parse.quote(version)}/{urllib.parse.quote(name)}'
    req = urllib.request.Request(f'{API}/projects/{PROJECT}{url_path}', method='PUT',
                                 headers={'JOB-TOKEN': os.environ['CI_JOB_TOKEN'],
                                          'Content-Length': str(path.stat().st_size)})
    with open(path, 'rb') as f:
        req.data = f
        try:
            urllib.request.urlopen(req, timeout=3600).read()
        except urllib.error.HTTPError as e:
            raise SystemExit(f'upload {name}: HTTP {e.code} {e.read().decode(errors="replace")[:500]}')
    print(f'uploaded {name} ({path.stat().st_size // 1000000} MB)')
    return {'name': name, 'url': f'{API}/projects/{PROJECT}{url_path}', 'link_type': 'package',
            'direct_asset_path': f'/{name}'}


def publish(dist):
    version = os.environ['GH_NEEDS_VERSION_VERSION']
    if os.environ.get('GH_NEEDS_VERSION_PUBLISH') != 'true':
        print(f'{version}: not published (dry run or development build); the files are this job\'s artifacts')
        return
    files = sorted({Path(f) for p in PATTERNS for f in glob.glob(str(Path(dist) / p))})
    if not any(f.suffix in ('.zip', '.apk') for f in files):
        raise SystemExit(f'no release files in {dist}')
    links = [upload(version, f) for f in files]
    notes = (Path(dist) / 'notes.md').read_text()
    release = call('POST', '/releases', {'tag_name': version, 'name': version, 'description': notes,
                                         'assets': {'links': links}})
    print(f'release {version}: {release.get("_links", {}).get("self", "created")}')


def vt_notes(report):
    version = os.environ['GH_NEEDS_VERSION_VERSION']
    if os.environ.get('GH_NEEDS_VERSION_PUBLISH') != 'true':
        print('not a published release: nothing to add')
        return
    section = subprocess.run([sys.executable, str(ROOT / 'tools/release/virustotal.py'), '--render-notes', report],
                             check=True, stdout=subprocess.PIPE, text=True).stdout
    if not section.strip():
        print('no VirusTotal results: release unchanged')
        return
    tag = urllib.parse.quote(version, safe='')
    release = call('GET', f'/releases/{tag}')
    call('PUT', f'/releases/{tag}', {'description': release.get('description', '') + '\n' + section})
    link = upload(version, Path(report))
    call('POST', f'/releases/{tag}/assets/links', link)
    print('VirusTotal section added')


if __name__ == '__main__':
    if len(sys.argv) != 3 or sys.argv[1] not in ('publish', 'vt-notes'):
        sys.exit(__doc__)
    {'publish': publish, 'vt-notes': vt_notes}[sys.argv[1]](sys.argv[2])
