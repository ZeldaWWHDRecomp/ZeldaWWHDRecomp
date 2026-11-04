import subprocess
import tempfile
import unittest
from pathlib import Path


class SeaCompass(unittest.TestCase):
    def test_complete_enabled_consumer(self):
        code = r'''

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t u32;
typedef int32_t s32;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint8_t u8;
typedef float f32;
typedef double f64;
static unsigned char self[0x60], actor[0x330], pane[0x60], other[0x60];
static f64 bias;
static f32 scale, units;
static unsigned calls, accesses;
static int found, mutation;
static u32 trace[32];
static void *read_at(u32 address) {
  assert(accesses < 32);
  trace[accesses++] = address;
  return (void *)address;
}
static void *data_at(unsigned int address) {
  if (address == 0x100EE848)
    return &bias;
  if (address == 0x100EE850)
    return &scale;
  if (address == 0x100EE854)
    return &units;
  assert(0);
  return 0;
}
#define R_SEA_COMPASS(t, a) (*(t *)read_at(a))
#define WWHD_GAME_DATA(a, t) (*(t *)data_at(a))
#define WWHD_GAME_FUNC(a, r, n, args) r n args
#define WWHD_REPLACE(a, r, n, args) r n args
#define init_invert_sea_compass_x_axis 1
#include "sea_compass.h"
u32 randomizer_compass_find_actor(u32 predicate, u32 argument) {
  calls++;
  assert(predicate == 0x025E121C && *(u16 *)argument == 0xA5);
  if (mutation) {
    *(u32 *)(self + 0x50) = (u32)other;
    *(u16 *)(actor + 0x32A) = 0x1234;
  }
  return found ? (u32)actor : 0;
}
static void reset(void) {
  memset(self, 0x53, sizeof self);
  memset(actor, 0x73, sizeof actor);
  memset(pane, 0xA5, sizeof pane);
  memset(other, 0x5A, sizeof other);
  *(u32 *)(self + 0x50) = (u32)pane;
  union {
    uint64_t bits;
    f64 value;
  } b = {.bits = 0x4330000080000000ull};
  bias = b.value;
  scale = 0.0054931640625f;
  units = 0.017453292f;
  calls = accesses = 0;
  found = 1;
  mutation = 0;
}
static void verify(unsigned char *selected, unsigned int heading) {
  unsigned char expected[sizeof pane];
  memcpy(expected, selected, sizeof expected);
  const s32 adjusted = (s16)((uint16_t)heading + 0x8000u);
  const f32 first = (f32)adjusted * scale, result = first * units;
  memcpy(expected + 0x30, &result, 4);
  expected[0x44] |= 0x10;
  randomizer_inverted_sea_compass((u32)self);
  assert(calls == 1 && memcmp(selected, expected, sizeof expected) == 0);
  assert(accesses == 10);
  const u32 wanted[] = {(u32)actor + 0x32A,   (u32)self + 0x50,
                        (u32)selected + 0x28, (u32)selected + 0x2C,
                        (u32)selected + 0x30, (u32)selected + 0x28,
                        (u32)selected + 0x44, (u32)selected + 0x2C,
                        (u32)selected + 0x30, (u32)selected + 0x44};
  assert(memcmp(trace, wanted, sizeof wanted) == 0);
}
int main(void) {
  unsigned cases = 0;
  const unsigned int xy[][2] = {{0, 0x80000000u},
                                {0x7FC12345, 0xFF812345},
                                {0x7F800000, 0xFF800000},
                                {0x3F800000, 0xBF800000}};
  for (unsigned heading = 0; heading < 65536; heading++)
    for (unsigned pattern = 0; pattern < 4; pattern++) {
      reset();
      *(u16 *)(actor + 0x32A) = heading;
      memcpy(pane + 0x28, &xy[pattern][0], 4);
      memcpy(pane + 0x2C, &xy[pattern][1], 4);
      pane[0x44] = (unsigned char)heading;
      verify(pane, heading);
      cases++;
    }
  for (unsigned flag = 0; flag < 256; flag++) {
    reset();
    mutation = 1;
    other[0x44] = flag;
    unsigned char original[sizeof pane];
    memcpy(original, pane, sizeof pane);
    verify(other, 0x1234);
    assert(memcmp(pane, original, sizeof pane) == 0);
    cases++;
  }
  for (unsigned nullself = 0; nullself < 2; nullself++) {
    reset();
    found = 0;
    unsigned char before[sizeof pane];
    memcpy(before, pane, sizeof pane);
    randomizer_inverted_sea_compass(nullself ? 0 : (u32)self);
    assert(calls == 1 && accesses == 0 &&
           memcmp(pane, before, sizeof pane) == 0);
    cases++;
  }
  printf("%u complete sea-compass cases passed\n", cases);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / 'test.c'
            source.write_text(code)
            subprocess.run(['clang', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-lm', '-o', str(directory / 'test')], check=True)
            subprocess.run([str(directory / 'test')], check=True)

    def test_disabled_has_no_descriptors_and_flag_is_mandatory(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'disabled.c'
            for value, expected_error in ((None, 'Missing validated'), (2, 'Invalid sea-compass'), (0, None)):
                source.write_text((f'#define init_invert_sea_compass_x_axis {value}\n' if value is not None else '') +
                                  '#define WWHD_REPLACE(...) _Static_assert(0,"unexpected replacement");\n'
                                  '#define WWHD_GAME_FUNC(...) _Static_assert(0,"unexpected import");\n'
                                  '#include "sea_compass.h"\nint main(void){return 0;}\n')
                result = subprocess.run(['clang', '-fsyntax-only', '-I', str(Path(__file__).parent), str(source)], text=True, capture_output=True)
                if expected_error:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(expected_error, result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
