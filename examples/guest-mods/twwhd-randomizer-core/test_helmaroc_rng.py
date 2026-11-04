import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path


class HelmarocConsumer(unittest.TestCase):
    def test_complete_enabled_consumer(self):
        header = Path(__file__).with_name('helmaroc_rng.h').read_text()
        prefix = r'''


#include <assert.h>
#include <math.h>
#include <stdarg.h>
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
#define HRNG_FIELD_VIEW struct
#define HRNG_READ_POINTER(a) pointer_at(a)
#define init_fix_helmaroc_rng 1
#include "helmaroc_rng.h"
static hrng_actor boss, player;
static hrng_morf morph, other_morph;
static hrng_hio hio;
static unsigned char game[0x7000], registers[0x1000];
static f32 matrix[12];
static u32 matrix_pointer;
static f64 random_value, centered_value, player_distance, square_override;
static unsigned randoms, centereds, lines, particles, animations, reports,
    moves, grounds, external_calls;
static f64 random_max[16];
static unsigned trace[4096], nt;
static int line_hit, land_area, mutation;
static s16 player_angle;

static int last_animation, last_sound, expected_tail, last_external;
static void hit(unsigned n) {
  assert(nt < 4096);
  trace[nt++] = n;
}
static void *data_at(unsigned int address) {
  switch (address) {
  case 0x1046191C:
    return &hio;
  case 0x1047B610:
    return registers + 8;
  case 0x1047B688:
    return registers + 0x80;
  case 0x1018C7B0:
    return &matrix_pointer;
  case 0x10007F68:
    return (void *)"ACTION MODE  %d";
  case 0x10007F78:
    return (void *)"MOVE MODE    %d";
  default:
    assert(0);
    return0;
  }
}
static u32 pointer_at(u32 address) {
  if (address == (u32)game + 0x5B2C)
    return (u32)&player;
  if (address == (u32)game + 0x5AB0)
    return 0x4444;
  assert(0);
  return0;
}
static void external_action(hrng_actor *a, int code) {
  assert(a == &boss);
  external_calls++;
  last_external = code;
  if (mutation == 4) {
    a->attention_info.flags = 0xABCD;
    a->m2591 = 1;
    a->mpMorf = &other_morph;
    other_morph.state = 1;
    a->m2588 = 66;
    a->m2590 = 2;
    a->m258C = -7;
    expected_tail = 1;
  }
}
'''
        overrides = {
            'hrng_game': 'return(u32)game;',
            'hrng_sqrt': 'return sqrt(a0);',
            'hrng_cM_rndF': 'assert(randoms<16);random_max[randoms++]=a0;if(mutation==1&&randoms==1)boss.m2FC=2;if(mutation==2){boss.m2593=99;boss.m8F8=9;boss.mAction=99;boss.mState=99;}return random_value;',
            'hrng_cM_rndFX': 'centereds++;return centered_value;',
            'hrng_PSVECSquareMag': 'return square_override!=-1?square_override:(f64)((a0->x*a0->x+a0->y*a0->y)+a0->z*a0->z);',
            'hrng_vec_mi': 'hrng_vec out={a0->x-a2->x,a0->y-a2->y,a0->z-a2->z};*a1=out;',
            'hrng_vec_pl': 'hrng_vec out={a0->x+a2->x,a0->y+a2->y,a0->z+a2->z};*a1=out;',
            'hrng_PSVECAdd': 'hrng_vec out={a0->x+a1->x,a0->y+a1->y,a0->z+a1->z};*a2=out;',
            'hrng_BdkMove_landAreaCheck': 'assert(a0==&boss.m2CC&&a1==1400);return land_area;',
            'hrng_MtxPosition': '*a1=*a0;',
            'hrng_cLib_addCalcPos2': 'f32*value=(f32*)a0;const f32*target=(const f32*)a1;for(unsigned i=0;i<3;i++){f32 delta=(target[i]-value[i])*a2;if(delta>a3)delta=a3;if(delta<-a3)delta=-a3;value[i]+=delta;}',
            'hrng_cLib_addCalcAngleS2': '*a0=a1;',
            'hrng_actor_angle': 'assert(a0==&boss&&a1==&player);if(mutation==8)player_distance=2000;return player_angle;',
            'hrng_actor_distance': 'assert(a0==&boss&&a1==&player);if(mutation==7){player.current.pos.x=123;player.current.pos.y=456;player.current.pos.z=789;}return player_distance;',
            'hrng_pos_move': 'assert(a0==&boss);moves++;',
            'hrng_ground_move': 'assert(a0==&boss);grounds++;',
            'hrng_anm_init': 'assert(a0==&boss);animations++;last_animation=a1;last_sound=a5;if(expected_tail){assert(a1==66&&a2==20&&a3==2&&a4==1&&a5==-7&&a6==0);boss.m2591=99;expected_tail=0;}',
            'hrng_line_ctor_native': 'memset(a0,0,sizeof(*a0));if(mutation==9)player.current.pos.y=123;',
            'hrng_line_destroy_native': 'assert(a1==0);assert(*(unsigned int*)(a0->bytes+0x58)==0x10007F38&&*(unsigned int*)(a0->bytes+0x64)==0x10007E88&&*(unsigned int*)(a0->bytes+0x20)==0x10007E78);',
            'hrng_line_cross': 'assert(a0==game+0x12A0);lines++;return line_hit&&lines>=(unsigned)line_hit?0x80000000u:0;',
            'hrng_dBgS_LinChk_Set': 'assert(a3==&boss&&a0&&a1&&a2);if(mutation==9)assert(a1->y==173);',
            'hrng_particle': 'assert(a0==0x4444&&(a1==0||a1==2)&&a3&&a11==0);particles++;return 0;',
            'hrng_shock': 'assert(a0==game+0x599C&&a2==-0x21&&a3->x==0&&a3->y==1&&a3->z==0);',
        }
        for name in ('fly_attack', 'wind_attack', 'kamen_demo', 'start', 'end', 't_fly', 't_lastattack', 't_down', 'BdkMove_afterFight'):
            overrides['hrng_' + name] = 'external_action(a0,' + str({'fly_attack':7,'wind_attack':8,'kamen_demo':9,'end':10,'start':15,'t_fly':100,'t_lastattack':102,'t_down':103,'BdkMove_afterFight':110}[name]) + ');'
        definitions = []
        ids = {}
        for number, match in enumerate(re.finditer(r'WWHD_GAME_FUNC\(0x[0-9A-F]+,\s*([^,]+),\s*(\w+),\s*\((.*?)\)\);', header, re.S), 1):
            result, name, arguments = match.groups()
            ids[name] = number
            if name == 'hrng_BdkMove_report':
                definitions.append(f'''void {name}(s32 x,s32 y,const char*format,...){{hit({number});assert(x==0x1E&&y==(reports?0x17C:0x168));va_list args;va_start(args,format);const int value=va_arg(args,int);va_end(args);assert(value==(reports?boss.mState:boss.mAction));reports++;if(mutation==3){{if(reports==1){{boss.mAction=3;boss.mState=99;}}else boss.mAction=15;}}}}''')
                continue
            parameters = []
            for index, argument in enumerate(arguments.split(',')):
                argument = argument.strip()
                if not argument or argument == 'void':
                    continue
                if re.search(r'\s+\w+$', argument) and argument.split()[-1] not in ('int', 'char'):
                    argument = re.sub(r'\s+\w+$', '', argument)
                elif re.search(r'\*\w+$', argument):
                    argument = re.sub(r'\w+$', '', argument)
                parameters.append(f'{argument} a{index}')
            unused = ''.join(f'(void)a{i};' for i in range(len(parameters)))
            body = overrides.get(name, '' if result.strip() == 'void' else 'return0;')
            definitions.append(f'{result} {name}({",".join(parameters) or "void"}){{hit({number});{unused}{body}}}')
        suffix = r'''


static void reset(void) {
  memset(&boss, 0, sizeof boss);
  memset(&player, 0, sizeof player);
  memset(&morph, 0, sizeof morph);
  memset(&other_morph, 0, sizeof other_morph);
  memset(&hio, 0, sizeof hio);
  memset(registers, 0, sizeof registers);
  memset(game, 0, sizeof game);
  boss.mpMorf = &morph;
  morph.rate = other_morph.rate = 1;
  hio.m018 = 3;
  hio.m01C = 1;
  hio.m020 = 2;
  boss.m8F8 = 3;
  boss.m2593 = 2;
  boss.m2EC[3] = 100;
  boss.m2EC[1] = 10;
  boss.m2CA = 3;
  random_value = centered_value = .75;
  player_distance = 500;
  square_override = -1;
  randoms = centereds = lines = particles = animations = reports = moves =
      grounds = external_calls = nt = 0;
  line_hit = 0;
  land_area = 1;
  mutation = 0;
  player_angle = 0;
  matrix_pointer = (u32)matrix;
  expected_tail = 0;
  last_animation = last_sound = last_external = -1;
}
int main(void) {
  unsigned cases = 0;
  const int actions[] = {0, 1,  2,  3,   4,   5,   6,   7, 8,
                         9, 10, 15, 100, 102, 103, 110, 99};
  const int states[] = {0, 1, 2, 3, 4, 5, 6, 7, 10, -2, 99};
  for (unsigned a = 0; a < 17; a++)
    for (unsigned st = 0; st < 11; st++)
      for (unsigned stop = 0; stop < 2; stop++)
        for (unsigned flags = 0; flags < 4; flags++) {
          reset();
          boss.mAction = actions[a];
          boss.mState = states[st];
          morph.state = stop;
          boss.mAcch.flags = flags * 0x10;
          randomizer_helmaroc_fixed_rng_move((u32)&boss);
          assert(reports == 2);
          assert(external_calls == (unsigned)(a >= 7 && a < 16));
          if (a >= 7 && a < 16)
            assert(last_external == actions[a]);
          assert(boss.attention_info.flags ==
                 ((actions[a] == 100 || actions[a] == 103 || actions[a] == 110)
                      ? 0
                      : 4));
          cases++;
        }
  const f64 random_edges[] = {-INFINITY, -1, 0, .29999999999999993, .3,
                              .5,        .8, 1, INFINITY,           NAN};
  for (unsigned value = 0; value < 10; value++)
    for (unsigned initial = 1; initial < 4; initial++) {
      reset();
      boss.mAction = 0;
      boss.mState = 0;
      boss.m2FC = initial;
      random_value = random_edges[value];
      randomizer_helmaroc_fixed_rng_move((u32)&boss);
      assert(boss.m2FC == (initial == 1 ? 2 : initial == 2 ? 1 : 3));
      assert(randoms == (initial == 2 ? 2 : 1) &&
             centereds == (initial == 3 ? 0 : 2));
      cases++;
    }
  for (unsigned initial = 0; initial < 2; initial++)
    for (unsigned mutate = 0; mutate < 2; mutate++) {
      reset();
      boss.mAction = 0;
      boss.mState = 1;
      boss.m2FC = initial;
      square_override = 1000000;
      mutation = mutate ? 2 : 0;
      random_value = NAN;
      randomizer_helmaroc_fixed_rng_move((u32)&boss);
      assert(boss.mAction == 2 && boss.mState == 0 && boss.m2593 == 0 &&
             randoms == initial);
      cases++;
    }
  reset();
  boss.mAction = 0;
  boss.mState = 0;
  boss.m2FC = 1;
  mutation = 1;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(boss.m2FC == 1);
  cases++;
  for (unsigned mode = 0; mode < 4; mode++)
    for (unsigned health = 0; health < 4; health++)
      for (unsigned wall = 0; wall < 2; wall++)
        for (unsigned value = 0; value < 10; value++) {
          reset();
          boss.mAction = 3;
          boss.mState = 1;
          boss.m2CA = 3;
          boss.m8F8 = health;
          hio.m014 = mode;
          line_hit = wall ? 1 : 0;
          random_value = random_edges[value];
          randomizer_helmaroc_fixed_rng_move((u32)&boss);
          const int wind = mode == 2 || (mode == 0 && wall);
          assert(boss.mAction == (wind ? 8 : mode == 3 ? 3 : 5));
          assert(boss.mState == 0);
          assert(randoms == (unsigned)(mode == 0 && !wall && health >= 2));
          assert(boss.m2586 == 2 && grounds == 1);
          assert(lines == (unsigned)(mode == 0 ? (wall ? 1 : 8) : 0));
          cases++;
        }
  reset();
  boss.mAction = 3;
  boss.mState = 1;
  player_distance = 400;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(boss.mAction == 6 && randoms == 0 && lines == 0);
  cases++;
  reset();
  boss.mAction = 3;
  boss.mState = 1;
  player_distance = 1400;
  random_value = .75;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(boss.mAction == 8 && randoms == 1 && lines == 0);
  cases++;
  reset();
  boss.mAction = 3;
  boss.mState = 1;
  mutation = 2;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(boss.mAction == 5 && boss.mState == 0 && randoms == 1);
  cases++;
  reset();
  boss.mAction = 0;
  mutation = 3;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(external_calls == 1 && boss.mAction == 15 && boss.mState == 99);
  cases++;
  for (unsigned a = 7; a < 16; a++) {
    reset();
    boss.mAction = actions[a];
    mutation = 4;
    randomizer_helmaroc_fixed_rng_move((u32)&boss);
    assert(external_calls == 1 && animations == 1 && expected_tail == 0 &&
           boss.m2591 == 0 && last_animation == 66 && last_sound == -7);
    assert(boss.attention_info.flags ==
           ((actions[a] == 100 || actions[a] == 103 || actions[a] == 110)
                ? 0
                : 0xABCD));
    cases++;
  }
  for (unsigned i = 0; i < 10; i++) {
    reset();
    boss.mAction = 3;
    boss.mState = 1;
    player_distance = 1400;
    random_value = random_edges[i];
    mutation = 2;
    randomizer_helmaroc_fixed_rng_move((u32)&boss);
    assert(boss.mAction == (random_edges[i] < .5 ? 4 : 8) && boss.mState == 0 &&
           randoms == 1);
    cases++;
    reset();
    boss.mAction = 3;
    boss.mState = 10;
    morph.state = 1;
    random_value = random_edges[i];
    randomizer_helmaroc_fixed_rng_move((u32)&boss);
    assert(boss.mAction == (random_edges[i] < .5 ? 1 : 3) &&
           boss.mState == (random_edges[i] < .5 ? 0 : 1) && randoms == 1);
    cases++;
  }
  reset();
  boss.mAction = 0;
  boss.mState = 0;
  boss.m2FC = 0;
  centered_value = 1000.000245;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(boss.m2CC.x == (f32)(6500.0 + centered_value));
  cases++;
  const f64 distances[] = {1499.9999999999998, 1500, 1500.0000000000002,
                           INFINITY, NAN};
  for (unsigned i = 0; i < 5; i++) {
    reset();
    boss.mAction = 0;
    boss.mState = 1;
    boss.m2FC = 1;
    square_override = distances[i] * distances[i];
    f64 returned = sqrt(square_override);
    randomizer_helmaroc_fixed_rng_move((u32)&boss);
    assert((boss.mAction == 2) == (returned < 1500));
    assert(randoms == (unsigned)(returned < 1500));
    cases++;
  }
  reset();
  boss.mAction = 3;
  boss.mState = 1;
  player.current.pos.x = 4706;
  player.current.pos.z = -4635;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(boss.mAction == 8 && randoms == 0 && lines == 0);
  cases++;
  const f32 frames[] = {
      -INFINITY, -2147483648.0f, -1,       0,  1, 4, 13, 16, 30,
      36,        2147483648.0f,  INFINITY, NAN};
  for (unsigned i = 0; i < 13; i++) {
    reset();
    boss.mAction = 6;
    boss.mState = 1;
    boss.m2EC[0] = 100;
    morph.frame = frames[i];
    randomizer_helmaroc_fixed_rng_move((u32)&boss);
    assert(boss.mAction == 6 && boss.mState == 1);
    cases++;
  }
  const f32 rates[] = {-INFINITY, -1, -0.0f, 0, 1, INFINITY, NAN};
  for (unsigned state = 0; state < 4; state++)
    for (unsigned i = 0; i < 7; i++) {
      reset();
      boss.mAction = 99;
      boss.m2591 = 1;
      boss.m2588 = 7;
      morph.state = state;
      morph.rate = rates[i];
      const unsigned stopped = (state & 1) || rates[i] == 0;
      randomizer_helmaroc_fixed_rng_move((u32)&boss);
      assert(animations == stopped && boss.m2591 == (stopped ? 0 : 1));
      cases++;
    }
  const unsigned int opaque[] = {0, 0x80000000u, 0x7FA12345u, 0xFF812345u,
                                 0x7F800000u};
  for (unsigned i = 0; i < 5; i++) {
    reset();
    boss.mAction = 0;
    boss.mState = 99;
    memcpy(&hio.m020, &opaque[i], 4);
    randomizer_helmaroc_fixed_rng_move((u32)&boss);
    assert(memcmp(&boss.m2E4, &opaque[i], 4) == 0);
    cases++;
  }
  unsigned int overlapping[] = {0x7FA12345u, 2, 3, 4};
  hrng_bdk_vbits((hrng_vec *)(overlapping + 1), (hrng_vec *)overlapping);
  for (unsigned i = 0; i < 4; i++)
    assert(overlapping[i] == 0x7FA12345u);
  cases++;
  reset();
  boss.mAction = 3;
  boss.mState = 99;
  mutation = 7;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(boss.m2CC.x == 123 && boss.m2CC.y == 456 && boss.m2CC.z == 789);
  cases++;
  reset();
  boss.mAction = 3;
  boss.mState = 1;
  mutation = 8;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(player_distance == 2000 && boss.mAction == 5 && randoms == 1);
  cases++;
  reset();
  boss.mAction = 3;
  boss.mState = 1;
  mutation = 9;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(lines == 8 && boss.mAction == 5);
  cases++;
  /* Native BGE skips on !LT and BLE skips on !GT, including unordered. */
  for (unsigned initial = 0; initial < 3; initial++) {
    reset();
    boss.mAction = 0;
    boss.mState = 0;
    boss.m2FC = initial;
    square_override = NAN;
    randomizer_helmaroc_fixed_rng_move((u32)&boss);
    assert(boss.mAction == 0 && boss.mState == 0 && moves == 1);
    cases++;
  }
  reset();
  boss.mAction = 3;
  boss.mState = 1;
  player_distance = NAN;
  random_value = .75;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(boss.mAction == 8 && boss.mState == 0 && randoms == 1 && lines == 0);
  cases++;
  reset();
  boss.mAction = 5;
  boss.mState = 2;
  morph.frame = NAN;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(boss.m2584 == 0 && boss.mAction == 5 && boss.mState == 2);
  cases++;
  reset();
  boss.mAction = 6;
  boss.mState = 1;
  boss.m2EC[0] = 100;
  player_distance = NAN;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(boss.mAction == 6 && boss.mState == 1);
  cases++;
  for (unsigned initial = 0; initial < 3; initial++) {
    reset();
    boss.mAction = 2;
    boss.mState = initial;
    square_override = NAN;
    randomizer_helmaroc_fixed_rng_move((u32)&boss);
    assert(boss.mAction == 2 && boss.mState == (s16)(initial ? initial : 1));
    cases++;
  }
  for (unsigned initial = 1; initial <= 2; initial++) {
    reset();
    boss.mAction = 4;
    boss.mState = initial;
    boss.m2EC[0] = 1;
    morph.frame = 23;
    boss.speed.y = NAN;
    *(f32 *)(registers + 8 + 8 * 0x90 + 9 * 4) = NAN;
    randomizer_helmaroc_fixed_rng_move((u32)&boss);
    assert(boss.mAction == 4 && boss.mState == 2 && isnan(boss.speed.y));
    cases++;
  }
  reset();
  boss.mAction = 2;
  boss.mState = 3;
  boss.current.pos.y = NAN;
  randomizer_helmaroc_fixed_rng_move((u32)&boss);
  assert(boss.mAction == 2 && boss.mState == 4 && boss.speed.y == -10);
  cases++;
  printf("%u complete Helmaroc cases passed\n", cases);
  return0;
}
'''
        source_code = (prefix + '\n'.join(definitions) + suffix).replace('return0', 'return 0')
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / 'test.c'
            source.write_text(source_code)
            subprocess.run([os.environ.get("CC", "cc"), '-O2', '-ffp-contract=off', '-fsanitize=signed-integer-overflow', '-fno-sanitize-recover=signed-integer-overflow', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-lm', '-o', str(directory / 'test')], check=True)
            subprocess.run([str(directory / 'test')], check=True)

    def test_runtime_condition_register_unordered_branches(self):
        code = r'''

#include "ppc.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
  const double values[] = {-INFINITY, -1500, -0.0, 0,    1,        36, 300,
                           600,       1000,  1300, 1500, INFINITY, NAN};
  unsigned cases = 0;
  for (unsigned a = 0; a < 13; ++a) {
    for (unsigned b = 0; b < 13; ++b) {
      Cpu cpu = {0};
      cr_set_f(&cpu, 0, values[a], values[b]);
      /* DB78/E798/E7DC/EA04/EFB4: BGE skips a less-than body. */
      const int bge_skip = !cpu.cr[0];
      assert(!bge_skip == (values[a] < values[b]));
      /* DA48/DB0C/F864: BLE skips a greater-than body. */
      const int ble_skip = !cpu.cr[1];
      assert(!ble_skip == (values[a] > values[b]));
      /* Positive BLT/BGT and negated-skip paths are equivalent. */
      assert(cpu.cr[0] == !bge_skip && cpu.cr[1] == !ble_skip);
      if (isnan(values[a]) || isnan(values[b])) {
        assert(cpu.cr[3] && bge_skip && ble_skip);
        assert(!cpu.cr[0] && !cpu.cr[1] && !cpu.cr[2]);
      }
      ++cases;
    }
  }
  printf("%u runtime CR branch pairs passed\n", cases);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / 'cr.c'
            source.write_text(code)
            include = Path(__file__).resolve().parents[3] / 'runtime/include'
            subprocess.run([os.environ.get("CC", "cc"), '-O2', '-Wall', '-Wextra', '-Werror', '-I', str(include), str(source), '-o', str(directory / 'cr')], check=True)
            subprocess.run([str(directory / 'cr')], check=True)

    def test_disabled_has_no_source_owner(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = Path(temporary) / 'disabled.c'
            for flag, error in ((None, 'Missing validated'), (2, 'Invalid Helmaroc'), (0, None)):
                source.write_text((f'#define init_fix_helmaroc_rng {flag}\n' if flag is not None else '') +
                                  '#define WWHD_GAME_FUNC(...) _Static_assert(0,"unexpected import");\n'
                                  '#define WWHD_REPLACE(...) _Static_assert(0,"unexpected owner");\n'
                                  '#include "helmaroc_rng.h"\nint main(void){return 0;}\n')
                result = subprocess.run([os.environ.get("CC", "cc"), '-fsyntax-only', '-I', str(Path(__file__).parent), str(source)], capture_output=True, text=True)
                if error:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn(error, result.stderr)
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == '__main__':
    unittest.main()
