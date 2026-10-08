# SPDX-License-Identifier: MPL-2.0
"""Emit address-only C tables for runtime unit tests; no executable is needed."""
import sys
import builds
from recomp import Recompiler

compiler = Recompiler.__new__(Recompiler)
compiler.build = builds.by_name("EU")
with open(sys.argv[1], "w", encoding="utf-8") as output:
    output.write('#include "guest_addr.h"\n')
    compiler.write_build_map(output)
