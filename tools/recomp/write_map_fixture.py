# SPDX-License-Identifier: MPL-2.0
"""Emit address-only C tables for runtime unit tests; no executable is needed.

usage: write_map_fixture.py OUT.c [BUILD]   (BUILD: a map in tools/recomp/builds, EU by default)"""
import sys
import builds
from recomp import Recompiler

compiler = Recompiler.__new__(Recompiler)
compiler.build = builds.by_name(sys.argv[2] if len(sys.argv) > 2 else "EU")
with open(sys.argv[1], "w", encoding="utf-8") as output:
    output.write('#include "guest_addr.h"\n')
    compiler.write_build_map(output)
