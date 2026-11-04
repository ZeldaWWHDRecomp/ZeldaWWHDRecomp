import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


class GrappleConsumer(unittest.TestCase):
    def test_complete_consumer(self):
        header = Path(__file__).with_name('traversal_grapple.h').read_text()
        prefix = r'''

#include <assert.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uintptr_t u32;
typedef int32_t s32;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint8_t u8;
typedef int8_t s8;
typedef float f32;
typedef double f64;
static void *data_at(unsigned int address);
static u32 pointer_at(u32 address);
#define WWHD_GAME_DATA(a, t) (*(t *)data_at(a))
#define WWHD_GAME_FUNC(a, r, n, args) r n args
#define WWHD_REPLACE(a, r, n, args) r n args
#define TRG_FIELD_VIEW struct
#define TRG_READ_POINTER(a) pointer_at(a)
#include "traversal_grapple.h"
static trg_actor rope, player, target, boss, dragon;
static trg_camera camera;
static trg_emitter guide;
static trg_regs regs;
static trg_hio hio;
static f32 rope_scale, coil_scale, sine[16384], matrix[12];
static u32 boss_ptr, dragon_ptr, guide_ptr, matrix_ptr, save_ptr;
static u8 game[0x7000], save[0x1000], virtuals[0x100];
static int locked, ready, fly, trigger, bg, attack, search_found, line_hit,
    event_bit, mutation;
static unsigned calls[70], trace[4096], trace_count;
static f64 stick_x, stick_y, square_override;
static unsigned event_writes;
static f32 recorded_step, recorded_f8, recorded_f8_step;
static void hit(unsigned n) {
  assert(trace_count < 4096);
  trace[trace_count++] = n;
  calls[n]++;
}
static u32 virtual_ready(trg_actor *a) {
  hit(60);
  assert(a == &player);
  return ready;
}
static u32 virtual_fly(trg_actor *a) {
  hit(61);
  assert(a == &player);
  return fly;
}
static void *data_at(unsigned int address) {
  switch (address) {
  case 0x1047B608:
    return &regs;
  case 0x1046469C:
    return &hio;
  case 0x10464674:
    return &rope_scale;
  case 0x10464678:
    return &boss_ptr;
  case 0x1046467C:
    return &dragon_ptr;
  case 0x10475628:
    return &guide_ptr;
  case 0x1018C7B0:
    return &matrix_ptr;
  case 0x101F84DC:
    return &save_ptr;
  case 0x100110C8:
    return &coil_scale;
  case 0x104A44F8:
    return sine;
  default:
    assert(0);
    return 0;
  }
}
static u32 pointer_at(u32 address) {
  if (address == (u32)game + 0x5B2C || address == (u32)game + 0x5B34)
    return (u32)&player;
  if (address == (u32)game + 0x4858)
    return (u32)&camera;
  assert(0);
  return0;
}
'''
        overrides = {
            'trg_game': 'return (u32)game;',
            'trg_sqrt': 'return sqrt(a0);',
            'trg_event_test_native': 'assert(a0==(u32)save+0x644);return event_bit;',
            'trg_event_set_native': 'assert(a0==(u32)save+0x644);event_writes|=(unsigned)a1;',
            'trg_himo2_LockonTruth': 'assert(a0==(u32)game+0x5804);return locked;',
            'trg_himo2_LockonTarget': 'assert(a0==(u32)game+0x5804&&a1==0);return(u32)&target;',
            'trg_himo2_cameraStop': 'assert(a0==camera.mCamera&&rope.m24D9==-1);',
            'trg_himo2_cameraStart': 'assert(a0==camera.mCamera);',
            'trg_himo2_cameraTrim': 'assert(a0==camera.mCamera&&(a1==0||a1==2));',
            'trg_himo2_cameraReset': 'assert(a0==camera.mCamera&&a1&&a2);',
            'trg_himo2_cameraSet': 'assert(a0==camera.mCamera&&a1&&a2&&a4==rope.m24F4);',
            'trg_himo2_padTrigX': 'assert(a0==0);return trigger;',
            'trg_himo2_padTrigY': 'assert(a0==0);return 0;',
            'trg_himo2_padTrigZ': 'assert(a0==0);return 0;',
            'trg_himo2_subStickX': 'assert(a0==0);return stick_x;',
            'trg_himo2_subStickY': 'assert(a0==0);return stick_y;',
            'trg_PSVECSquareMag': 'return square_override>=0?square_override:(f64)((a0->x*a0->x+a0->y*a0->y)+a0->z*a0->z);',
            'trg_cM_atan2s': 'return a0==a0&&a1==a1?(s16)(s32)(atan2(a0,a1)*65536/6.283185307179586):0;',
            'trg_cLib_addCalc2': 'if(a0==&rope.m24B4)recorded_step=a3;if(a0==&rope.m24F8){recorded_f8=a1;recorded_f8_step=a3;}f32 delta=(a1-*a0)*a2;if(delta>a3)delta=a3;if(delta<-a3)delta=-a3;*a0+=delta;if(mutation==1&&a0==&rope.m24B4)rope.m24BC=26;',
            'trg_cLib_addCalc0': 'f32 delta=-*a0*a1;if(delta>a2)delta=a2;if(delta<-a2)delta=-a2;*a0+=delta;',
            'trg_cLib_addCalcPos2': 'f32*value=(f32*)a0;const f32*target=(const f32*)a1;for(unsigned i=0;i<3;i++){f32 delta=(target[i]-value[i])*a2;if(delta>a3)delta=a3;if(delta<-a3)delta=-a3;value[i]+=delta;}',
            'trg_cLib_addCalcAngleS2': '*a0=a1;',
            'trg_MtxPosition': '*a1=*a0;',
            'trg_PSVECAdd': 'trg_vec value={a0->x+a1->x,a0->y+a1->y,a0->z+a1->z};*a2=value;',
            'trg_vec_sub': 'trg_vec value={a0->x-a2->x,a0->y-a2->y,a0->z-a2->z};*a1=value;',
            'trg_vec_add': 'trg_vec value={a0->x+a2->x,a0->y+a2->y,a0->z+a2->z};*a1=value;',
            'trg_fopAcIt_Judge': 'assert(a0==0x025E1234&&a1);return search_found?&target:0;',
            'trg_himo2_playerPosExternal': 'assert(a0==&rope);if(mutation==2)rope.m02A2=0;',
            'trg_himo2_bgCheckExternal': 'assert(a0==&rope);return bg;',
            'trg_attack_hit': 'assert(a0==&rope.m2050);return attack;',
            'trg_line_native_ctor': 'memset(a0,0,sizeof(*a0));',
            'trg_line_set': 'assert(a1==&rope.m24E8&&a2==&rope.m24DC&&a3==&rope);memcpy(a0->bytes+0x30,a2,12);',
            'trg_line_cross': 'assert(a0==game+0x12A0);if(line_hit){const trg_vec point={100,200,300};memcpy(a1->bytes+0x30,&point,12);}return line_hit;',
            'trg_himo2_destroyLineCheck': 'assert(a1==0);assert(*(unsigned int*)(a0->bytes+0x58)==0x10010ED4&&*(unsigned int*)(a0->bytes+0x64)==0x10010E24&&*(unsigned int*)(a0->bytes+0x20)==0x10010E14);',
        }
        definitions = []
        ids = {}
        for number, match in enumerate(re.finditer(r'WWHD_GAME_FUNC\(0x[0-9A-F]+,\s*([^,]+),\s*(\w+),\s*\((.*?)\)\);', header, re.S), 1):
            result, name, arguments = match.groups()
            parameters = []
            for index, argument in enumerate(arguments.split(',')):
                argument = argument.strip()
                if argument == 'void' or not argument:
                    continue
                # Strip the optional source parameter name; definitions use positional aN.
                if re.search(r'\s+\w+$', argument) and argument.split()[-1] not in ('int', 'char'):
                    argument = re.sub(r'\s+\w+$', '', argument)
                elif re.search(r'\*\w+$', argument):
                    argument = re.sub(r'\w+$', '', argument)
                parameters.append(f'{argument} a{index}')
            ids[name] = number
            body = overrides.get(name, '' if result.strip() == 'void' else 'return 0;')
            unused = ''.join(f'(void)a{i};' for i in range(len(parameters)))
            definitions.append(f'{result} {name}({",".join(parameters) or "void"}){{hit({number});{unused}{body}}}')
        suffix = r'''

static unsigned first(unsigned id) {
  for (unsigned i = 0; i < trace_count; i++)
    if (trace[i] == id)
      return i;
  assert(0);
  return 0;
}

static void reset(void) {
  memset(&rope, 0, sizeof(rope));
  memset(&player, 0, sizeof(player));
  memset(&target, 0, sizeof(target));
  memset(&boss, 0, sizeof(boss));
  memset(&dragon, 0, sizeof(dragon));
  memset(&regs, 0, sizeof(regs));
  memset(&camera, 0, sizeof(camera));
  memset(game, 0, sizeof(game));
  memset(&guide, 0, sizeof(guide));
  memset(calls, 0, sizeof(calls));
  trace_count = event_writes = 0;
  locked = ready = fly = trigger = bg = attack = line_hit = mutation = 0;
  event_bit = search_found = 1;
  stick_x = stick_y = 0;
  square_override = -1;
  recorded_step = recorded_f8 = recorded_f8_step = -1;
  coil_scale = 25;
  hio.m20 = 2;
  hio.m1C = 3;
  boss_ptr = (u32)&boss;
  dragon_ptr = (u32)&dragon;
  guide_ptr = (u32)&guide;
  matrix_ptr = (u32)matrix;
  save_ptr = (u32)save;
  u32 fn = (u32)virtual_ready;
  memcpy(virtuals + 0xDC, &fn, sizeof(fn));
  fn = (u32)virtual_fly;
  memcpy(virtuals + 0x4C, &fn, sizeof(fn));
  player.__vtbl = (u32)virtuals;
  rope.m217C = &target;
  rope.m2180 = 7;
  target.scale.y = 1;
  target.current.pos.x = 100;
  target.current.pos.z = 200;
  target.eyePos = target.current.pos;
  rope.m24B4 = -144;
  rope.m24FC = 1;
  rope.m2500 = 1;
  rope.m24BC = 10;
  rope.m0308 = 40;
  rope.m029C = 50;
  rope.m02A0 = 2;
  rope.m02A2 = 2;
  rope.m02A4 = 2;
  rope.m02CC = 50;
  for (unsigned i = 0; i < 16384; i++)
    sine[i] = (i & 1) ? 1 : 0;
}
int main(void) {
  unsigned cases = 0;
  const int states[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
  const int demos[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  for (unsigned s = 0; s < 13; s++)
    for (unsigned d = 0; d < 12; d++)
      for (unsigned param = 0; param < 5; param++)
        for (unsigned flags = 0; flags < 4; flags++) {
          if (states[s] == 5 && demos[d] != 0)
            continue;
          reset();
          rope.m02DC = states[s];
          rope.m24D9 = demos[d];
          rope.mParameters = param;
          target.mParameters = (flags & 1) ? 0x10 : 0;
          locked = flags >> 1;
          rope.eventInfo.mCommand = 2;
          randomizer_grapple_move((u32)&rope);
          assert(rope.m02CC >= 2);
          assert(rope.m0308 >= 0);
          cases++;
        }
  reset();
  rope.m02DC = 0;
  rope.mParameters = 1;
  memset(rope.m0310, 0xA5, sizeof(rope.m0310));
  regs.mChild[18].mShortRegs[0] = 7;
  randomizer_grapple_move((u32)&rope);
  assert(rope.m0308 == 17 && rope.m02DC == 2 && rope.m02A0 == 10 &&
         rope.m02A2 == 3);
  for (unsigned i = 0; i < 100; i++) {
    assert(rope.m0310[i].m10.x == 0 && rope.m0310[i].m10.y == 0 &&
           rope.m0310[i].m10.z == 0);
    for (unsigned j = 0; j < sizeof(rope.m0310[i].pad); j++)
      assert(rope.m0310[i].pad[j] == 0xA5);
    for (unsigned j = 0; j < sizeof(rope.m0310[i].tail); j++)
      assert(rope.m0310[i].tail[j] == 0xA5);
  }
  cases++;
  for (unsigned high = 0; high < 2; high++)
    for (unsigned mutate = 0; mutate < 2; mutate++) {
      reset();
      rope.m02DC = 12;
      rope.m24D9 = 2;
      rope.m24B4 = -144;
      rope.m24BC = 10;
      rope.m24C4 = 40;
      target.mParameters = high ? 0x10 : 0;
      mutation = mutate;
      randomizer_grapple_move((u32)&rope);
      assert(recorded_step == (high ? 20 : 30));
      assert(rope.m24C0 == (mutate ? 26 : 10));
      assert(rope.m24BC == (mutate ? 34 : 18));
      assert(rope.m24C4 == 42);
      cases++;
    }
  for (unsigned command = 0; command < 2; command++) {
    reset();
    rope.m02DC = 12;
    rope.m24D9 = -1;
    rope.eventInfo.mCommand = command ? 2 : 0;
    randomizer_grapple_move((u32)&rope);
    if (command) {
      assert(rope.m24D9 == 2);
      assert(first(@STOP@) < first(@TRIM@) &&
             first(@TRIM@) < first(@SET@));
    } else
      assert(rope.m24D9 == -1 && (rope.eventInfo.mCondition & 2));
    cases++;
  }
  reset();
  rope.m02DC = 12;
  rope.m24D9 = 7;
  rope.m029C = 0;
  dragon.unk_4BA = 0;
  line_hit = 1;
  randomizer_grapple_move((u32)&rope);
  assert(rope.m24D9 == 8 && rope.m029C == 210);
  assert(rope.m24DC.x == 100 && rope.m24DC.y == 200 && rope.m24DC.z == 300);
  assert(first(@CTOR@) < first(@LINESET@) &&
         first(@LINESET@) < first(@CROSS@) &&
         first(@CROSS@) < first(@DESTROY@));
  cases++;
  for (unsigned state = 0; state < 12; state++) {
    reset();
    rope.m02DC = state;
    rope.m24D9 = 0;
    rope.current.pos.x = 100;
    rope.m2500 = 0;
    rope.mResetFlg0 = 0;
    player.mResetFlg0 = 0x40000000;
    rope.eventInfo.mCommand = 0;
    target.name = 0xFA;
    target.health = 7;
    randomizer_grapple_move((u32)&rope);
    if (state != 0) {
      assert(rope.m02DC == (state == 11 ? 0 : 9) && target.health == 0 &&
             rope.m217C == 0);
    }
    cases++;
  }
  for (unsigned mut = 0; mut < 2; mut++) {
    reset();
    rope.m02DC = 2;
    target.current.pos.x = 2000;
    target.current.pos.z = 0;
    attack = 1;
    mutation = mut ? 2 : 0;
    randomizer_grapple_move((u32)&rope);
    assert(rope.m02DC == (mut ? 9 : 2));
    assert((rope.m217C == 0) == (mut != 0));
    cases++;
  }
  for (unsigned timer = 11; timer <= 12; timer++) {
    reset();
    rope.m02DC = 2;
    rope.m0308 = timer;
    target.current.pos.x = NAN;
    randomizer_grapple_move((u32)&rope);
    assert(rope.m02DC == (timer == 11 ? 10 : 2));
    cases++;
  }
  const s16 timers[] = {-32768, -1, 0, 1, 2, 10, 32767};
  for (unsigned i = 0; i < 7; i++) {
    reset();
    rope.m02DC = 12;
    rope.m0308 = rope.m02A2 = rope.m029C = rope.m029E = rope.m02A0 = timers[i];
    randomizer_grapple_move((u32)&rope);
    s16 expected = timers[i] ? (s16)((uint16_t)timers[i] - 1) : 0;
    assert(rope.m0308 == expected && rope.m02A2 == expected &&
           rope.m029C == expected && rope.m029E == expected &&
           rope.m02A0 == expected);
    cases++;
  }
  const f64 sticks[] = {
      -INFINITY, -0.10000000149011612, -0.1000000014901161, -0.0,
      0,         0.1000000014901161,   0.10000000149011612, INFINITY,
      NAN};
  for (unsigned i = 0; i < 9; i++) {
    reset();
    rope.m02DC = 12;
    rope.m24D9 = 5;
    stick_y = sticks[i];
    fly = 1;
    randomizer_grapple_move((u32)&rope);
    f32 expected = !(sticks[i] > -.1f) ? 70 : !(sticks[i] < .1f) ? 20 : -1;
    assert(recorded_f8 == expected);
    if (expected != -1) {
      f32 step = (f32)(fabs(sticks[i]) * 3);
      assert((step != step && recorded_f8_step != recorded_f8_step) ||
             recorded_f8_step == step);
    }
    cases++;
  }
  const f64 extreme_sticks[] = {-INFINITY,-2147484.0,-2147483.648,-1.0,0,1.0,2147483.647,2147484.0,INFINITY,NAN};
  const s16 initial_yaws[] = {-32768,-1,0,1,32767};
  for(unsigned i=0;i<10;i++)for(unsigned j=0;j<5;j++){
    reset();rope.m02DC=12;rope.m24D9=5;fly=1;
    stick_x=extreme_sticks[i];rope.m2512=initial_yaws[j];
    const s32 increment=trg_ftoi(trg_mul(stick_x,1000.0));
    const s16 expected=(s16)((uint16_t)initial_yaws[j]+(uint16_t)increment);
    randomizer_grapple_move((u32)&rope);
    assert(rope.m2512==expected && rope.m2510==expected);cases++;
  }
  for (unsigned i = 0; i < 65536; i++) {
    union {
      f64 value;
      unsigned long long bits;
    } input = {.bits = 0x3FD0000000000000ull + ((unsigned long long)i << 16)},
      rounded = input;
    rounded.bits =
        (rounded.bits & 0xFFFFFFFFF8000000ull) + (rounded.bits & 0x8000000ull);
    assert(trg_mul(5, input.value) == (f32)(5 * rounded.value));
    cases++;
  }
  const f64 numbers[] = {
      -INFINITY, -2147483649.0, -2147483648.0, -1.75,    -0.0, 0,
      1.75,      2147483647.0,  2147483648.0,  INFINITY, NAN};
  for (unsigned i = 0; i < 11; i++) {
    s32 expected = numbers[i] != numbers[i] || numbers[i] <= -2147483648.0
                       ? (s32)0x80000000u
                   : numbers[i] >= 2147483648.0 ? 0x7FFFFFFF
                                                : (s32)numbers[i];
    assert(trg_ftoi(numbers[i]) == expected);
    cases++;
  }
  printf("%u complete grapple cases passed\n", cases);
  return0;
}
'''
        for token, name in {'STOP': 'trg_himo2_cameraStop', 'TRIM': 'trg_himo2_cameraTrim', 'SET': 'trg_himo2_cameraSet', 'CTOR': 'trg_line_native_ctor', 'LINESET': 'trg_line_set', 'CROSS': 'trg_line_cross', 'DESTROY': 'trg_himo2_destroyLineCheck'}.items():
            suffix = suffix.replace('@' + token + '@', str(ids[name]))
        suffix = suffix.replace('return0', 'return 0')
        prefix = prefix.replace('return0', 'return 0')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / 'test.c'
            source.write_text(prefix + '\n'.join(definitions) + suffix)
            subprocess.run([os.environ.get("CC", "cc"), '-O2', '-ffp-contract=off', '-fsanitize=signed-integer-overflow', '-fno-sanitize-recover=signed-integer-overflow', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-lm', '-o', str(directory / 'test')], check=True)
            subprocess.run([str(directory / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
