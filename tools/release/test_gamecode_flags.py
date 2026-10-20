"""The game code's compile flags stay portable: code mods compile against them on the player's machine,
so the release packaging refuses any include path beyond runtime/include and the generated game code.
Run with the build directory (it needs compile_commands.json)."""
import sys

import package

flags = package.gamecode_flags(sys.argv[1])  # exits with the offending path if an extra include crept in
assert "-I{sdk}/include" in flags and "-I{gen}" in flags, flags
print("gamecode flags portable:", " ".join(flags))
