"""Compile and run the production Java safety code with the installed JDK."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'android/app/src/main/java/org/wwhdrecomp/wwhd'
OUT = ROOT / 'build/windwake-tests'
OUT.mkdir(parents=True, exist_ok=True)
sources = [SRC / (name + '.java') for name in ('SessionLease', 'SafeSaveArchive', 'TransactionalSaveStore', 'WindwakeSettings')]
subprocess.run(['javac', '-encoding', 'UTF-8', '-d', str(OUT), *map(str, sources), str(Path(__file__).with_name('WindwakeSafetyTest.java'))], check=True)
subprocess.run(['java', '-ea', '-cp', str(OUT), 'org.wwhdrecomp.wwhd.WindwakeSafetyTest'], check=True)
