"""Exercise the installed launcher's real controls using UIAutomator and verify native settings.

Only operates on dev.windwake.android. Does not import or modify any game/save.
The requested phone must already be authorized for ADB. Leaves the Poco/swipe defaults selected.
"""
import argparse
import json
from pathlib import Path
import re
import subprocess
import time
import xml.etree.ElementTree as E

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'build/qa'


def main(serial):
    OUT.mkdir(parents=True, exist_ok=True)
    adb = ROOT / 'build/android-sdk/platform-tools/adb.exe'

    def call(*args):
        return subprocess.run([str(adb), '-s', serial, *args], capture_output=True, check=True).stdout

    def dump():
        call('shell', 'uiautomator', 'dump', '/sdcard/windwake-ui.xml')
        call('pull', '/sdcard/windwake-ui.xml', str(OUT / 'current.xml'))
        return list(E.parse(OUT / 'current.xml').iter('node'))

    dimensions = list(map(int, re.findall(rb'(\d+)x(\d+)', call('shell', 'wm', 'size'))[-1]))

    def tap(text, index=0):
        matches = [n for n in dump() if n.get('text') == text]
        if len(matches) <= index:
            raise AssertionError('UI control not visible: ' + text)
        x1, y1, x2, y2 = map(int, re.findall(r'\d+', matches[index].get('bounds')))
        assert 0 <= x1 < x2 <= dimensions[0] and 0 <= y1 < y2 <= dimensions[1], 'Off-screen control: ' + text
        call('shell', 'input', 'tap', str((x1 + x2) // 2), str((y1 + y2) // 2))

    def settings():
        data = call('shell', 'cat', '/sdcard/Android/data/dev.windwake.android/files/config/wwhd/settings.ini').decode()
        return dict(line.split('=', 1) for line in data.splitlines() if '=' in line and not line.startswith('#'))

    def expect(expected):
        deadline = time.monotonic() + 5
        while True:
            actual = settings()
            if all(actual.get(k) == v for k, v in expected.items()):
                return
            if time.monotonic() > deadline:
                raise AssertionError('Settings were not published: ' + str(expected))
            time.sleep(0.1)  # await asynchronous fsync/publication, not a fixed UI timing assumption

    def scroll_to(label):
        for _ in range(6):
            matches = [n for n in dump() if n.get('text') == label]
            if matches:
                _, y1, _, y2 = map(int, re.findall(r'\d+', matches[0].get('bounds')))
                if 180 <= y1 < y2 < dimensions[1] - 120:
                    return
            call('shell', 'input', 'swipe', str(dimensions[0] // 2), str(dimensions[1] - 300),
                 str(dimensions[0] // 2), str(dimensions[1] // 2), '400')
        raise AssertionError('Control not reachable: ' + label)

    density = int(re.findall(rb'\d+', call('shell', 'wm', 'density'))[-1]) / 160
    def sensitivity(description, percent):
        node = next(n for n in dump() if n.get('content-desc') == description)
        x1, y1, x2, y2 = map(int, re.findall(r'\d+', node.get('bounds')))
        pad = round(8 * density)
        x = round(x1 + pad + (x2 - x1 - 2 * pad) * (percent - 25) / 275)
        call('shell', 'input', 'tap', str(x), str((y1 + y2) // 2))

    result = {}
    call('shell', 'am', 'start', '-W', '-n', 'dev.windwake.android/org.wwhdrecomp.wwhd.WindwakeActivity')
    tap('PERFIS')
    tap('Aplicar perfil', 1)
    expect({'dynResMin': '0.5', 'fps60': '0', 'heatGuard': 'normal'})
    result['economy_profile'] = True
    tap('Aplicar perfil', 0)
    expect({'dynResMin': '0.75', 'fps60': '0'})
    result['poco_profile'] = True
    tap('TOUCH')
    sensitivity('Sensibilidade da câmera', 150)
    sensitivity('Sensibilidade da mira', 85)
    tap('Analógico de movimento fixo')
    scroll_to('Aplicar controles'); tap('Aplicar controles')
    expect({'touchStyle': '1', 'touchRight': '0', 'touchSwipeSpeed': '1.50', 'windwakeAimSpeed': '0.85', 'windwakeFixedMove': '1'})
    result['independent_sensitivity_controls'] = True
    tap('TOUCH')
    sensitivity('Sensibilidade da câmera', 100)
    sensitivity('Sensibilidade da mira', 70)
    tap('Analógico de movimento fixo')
    scroll_to('Aplicar controles'); tap('Aplicar controles')
    expect({'touchSwipeSpeed': '1.00', 'windwakeAimSpeed': '0.70', 'windwakeFixedMove': '0', 'touchItemTap': '1'})
    result['phone_swipe_defaults'] = True
    tap('SAVES')
    assert any(n.get('text') == 'Exportar save' for n in dump())
    result['saves_page'] = True
    tap('INÍCIO')
    tap('Gerenciar instalação')
    labels = [n.get('text', '') for n in dump()]
    assert any('jogo' in label.lower() or 'game' in label.lower() for label in labels)
    result['setup_screen'] = True
    call('shell', 'input', 'keyevent', '4')
    dump()
    (OUT / 'home.png').write_bytes(call('exec-out', 'screencap', '-p'))
    result['settings'] = settings()
    (OUT / 'phone-validation.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    print(json.dumps(result))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--serial', required=True)
    main(parser.parse_args().serial)
