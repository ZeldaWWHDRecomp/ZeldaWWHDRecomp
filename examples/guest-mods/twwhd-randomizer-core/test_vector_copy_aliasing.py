"""Optimized actor vector copies must initialize typed float temporaries."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class VectorCopyAliasing(unittest.TestCase):
    def test_typed_local_copies(self):
        root = Path(__file__).parent
        helpers = (
            ("magtail_helpers.h", "rmt_copy_vector"),
            ("stalfos_helpers.h", "rst_copy_vector"),
            ("pig_helpers.h", "rp_copy_vector"),
            ("phantom_maze.h", "pgm_copy_vector"),
            ("arrow_move.h", "ram_copy"),
        )
        prefix = r'''
#include <stdint.h>
#include <string.h>
#include <assert.h>
typedef uintptr_t u32;
typedef unsigned char u8;
typedef float f32;
#define RMT32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RST32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RP_U32(a,o) (*(volatile unsigned int*)((a)+(o)))
#define RMT8(a,o) (*(volatile u8*)((a)+(o)))
#define RST8(a,o) (*(volatile u8*)((a)+(o)))
#define RP_U8(a,o) (*(volatile u8*)((a)+(o)))
#define COPY_TYPE_u32 unsigned int
#define COPY_TYPE_u8 u8
#define R_PGM(t,a) (*(volatile COPY_TYPE_##t*)(a))
#define R_ARROW(t,a) (*(volatile COPY_TYPE_##t*)(a))
'''
        with tempfile.TemporaryDirectory() as temp:
            directory = Path(temp)
            for header, name in helpers:
                with self.subTest(helper=name):
                    source = (root / header).read_text()
                    start = source.index("static void " + name + "(")
                    end = source.index("}", start) + 1
                    # These helpers contain one loop at most. Include its closing
                    # brace if it introduces a separate compound statement.
                    while source[start:end].count("{") > source[start:end].count("}"):
                        end = source.index("}", end) + 1
                    code = prefix + source[start:end] + r'''
__attribute__((noinline)) static float copied_y(u32 source) {
    float local[3];
    COPY((u32)local, source);
    local[1] += 100.0f;
    return local[1];
}
int main(void) {
    float input[3] = {-12.5f, 203.25f, 47.0f};
    assert(copied_y((u32)input) == 303.25f);
    /* Preserve representation, including signed zero and NaN payloads. */
    unsigned int bits[3] = {0x80000000u, 0x7FC12345u, 0x3F800000u};
    float local[3]; unsigned int output[3];
    COPY((u32)local, (u32)bits);
    memcpy(output, local, sizeof output);
    assert(memcmp(output, bits, sizeof bits) == 0);
    COPY((u32)local, (u32)local);
    memcpy(output, local, sizeof output);
    assert(memcmp(output, bits, sizeof bits) == 0);
    return 0;
}
'''.replace("COPY", name)
                    path = directory / (name + ".c")
                    executable = directory / name
                    path.write_text(code)
                    subprocess.run([
                        os.environ.get("CC", "cc"), "-O2", "-fstrict-aliasing",
                        "-Wall", "-Wextra", "-Werror", str(path), "-o", str(executable),
                    ], check=True)
                    subprocess.run([str(executable)], check=True)



if __name__ == "__main__":
    unittest.main()
