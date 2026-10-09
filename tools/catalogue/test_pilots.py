#!/usr/bin/env python3
import subprocess
import sys
import tempfile
from pathlib import Path
from make_pilots import generate

with tempfile.TemporaryDirectory(prefix="wwhd-catalogue-pilots-") as temporary:
    root = Path(temporary)
    generate(root)
    subprocess.run([sys.argv[1], "--catalogue-pilots", str(root), sys.executable], check=True)
