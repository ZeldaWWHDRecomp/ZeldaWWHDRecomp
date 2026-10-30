"""Run upstream's real touch layout/preset/stick contracts with the installed JDK."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'android/app/src/main/java/org/wwhdrecomp/wwhd'
OUT = ROOT / 'build/windwake-tests'
OUT.mkdir(parents=True, exist_ok=True)
names = ['TouchLayout', 'TouchPresets', 'TouchStick']
sources = [SRC / (name + '.java') for name in names] + [ROOT / 'tools/android' / (name + 'Test.java') for name in names]
subprocess.run(['javac', '-encoding', 'UTF-8', '-d', str(OUT), *map(str, sources)], check=True)
for name in names:
    subprocess.run(['java', '-ea', '-cp', str(OUT), 'org.wwhdrecomp.wwhd.' + name + 'Test'], check=True)
subprocess.run(['javac', '-encoding', 'UTF-8', '-d', str(OUT), str(SRC / 'WindwakeInputPolicy.java'),
                str(Path(__file__).with_name('WindwakeInputPolicyTest.java'))], check=True)
subprocess.run(['java', '-ea', '-cp', str(OUT), 'org.wwhdrecomp.wwhd.WindwakeInputPolicyTest'], check=True)
subprocess.run(['javac', '-encoding', 'UTF-8', '-cp', str(OUT), '-d', str(OUT), str(SRC / 'WindwakeTouchProfile.java'), str(SRC / 'ContextPack.java'), str(SRC / 'WindwakeScopeState.java'),
                str(Path(__file__).with_name('WindwakeTouchProfileTest.java'))], check=True)
subprocess.run(['java', '-ea', '-cp', str(OUT), 'org.wwhdrecomp.wwhd.WindwakeTouchProfileTest'], check=True)
