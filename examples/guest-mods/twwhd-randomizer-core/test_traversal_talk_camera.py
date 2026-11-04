import subprocess
import tempfile
import unittest
from pathlib import Path


class TalkCamera(unittest.TestCase):
    def test_complete_consumer(self):
        code = r'''


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
typedef float f32;
typedef double f64;
static unsigned char game[0x6000], style_data[0x10000], name_data[0x100],
    angle_data[16];
static void *global_data(uint32_t address) {
  switch (address) {
  case 0x1004487C:
    return style_data;
  case 0x1004ACDC:
    return name_data;
  case 0x101FF354:
    return angle_data;
  case 0x101FF358:
    return angle_data + 4;
  case 0x101FF35C:
    return angle_data + 8;
  default:
    assert(0);
    return 0;
  }
}
#define WWHD_GAME_DATA(a, t) (*(t *)global_data(a))
#define WWHD_GAME_FUNC(a, r, n, args) r n args
#define WWHD_REPLACE(a, r, n, args) r n args
#include "traversal_talk_camera.h"
static rtc_camera camera;
static rtc_actor actors[3];
static s32 shot, mutation;
static unsigned calls[50], degree_calls, line_calls, collision_calls;
static u32 table_actor;
static f64 radius_result, degree_result;
static char trace[32768];
static unsigned trace_n;
static void hit(unsigned id) {
  assert(trace_n < sizeof(trace));
  trace[trace_n++] = (char)id;
  calls[id]++;
}
u32 rtc_game(void) {
  hit(1);
  return (u32)game;
}
s16 *rtc_talk_angleFromShort(s16 *a, s16 v) {
  hit(2);
  *a = v;
  return a;
}
void rtc_talk_angleSubtract(const void *a, s16 *out, const void *b) {
  hit(3);
  *out = (s16)((unsigned)*(const s16 *)a - (unsigned)*(const s16 *)b);
}
void rtc_talk_angleMultiply(const void *a, s16 *out, f32 factor) {
  hit(4);
  *out = (s16)(s32)(*(const s16 *)a * factor);
}
void rtc_talk_angleAdd(const void *a, s16 *out, const void *b) {
  hit(5);
  *out = (s16)((unsigned)*(const s16 *)a + (unsigned)*(const s16 *)b);
}
f64 rtc_talk_angleCos(const void *a) {
  hit(6);
  return cos(*(const s16 *)a * (6.283185307179586 / 65536));
}
u32 rtc_talk_ChkCamera(void *ccs, rtc_vec *a, rtc_vec *b, f32 radius,
                       rtc_actor *x, rtc_actor *y) {
  hit(7);
  assert(ccs == game + 0x26A4 && a && b && radius == 15 && x && y);
  collision_calls++;
  return mutation == 8 && collision_calls < 4;
}
void rtc_talk_globeXyz(rtc_globe *g, rtc_vec *out) {
  hit(8);
  const double pitch = g->mV * (6.283185307179586 / 65536),
               yaw = g->mU * (6.283185307179586 / 65536);
  out->x = (f32)(g->mRadius * cos(pitch) * sin(yaw));
  out->y = (f32)(g->mRadius * sin(pitch));
  out->z = (f32)(g->mRadius * cos(pitch) * cos(yaw));
}
void rtc_talk_globeFromVector(rtc_globe *g, rtc_vec *v) {
  hit(9);
  g->mRadius = sqrtf(v->x * v->x + v->y * v->y + v->z * v->z);
  g->mV = (s16)(atan2(v->y, hypot(v->x, v->z)) * 65536 / 6.283185307179586);
  g->mU = (s16)(atan2(v->x, v->z) * 65536 / 6.283185307179586);
}
s16 *rtc_talk_angleFromDegrees(s16 *a, f32 degrees) {
  hit(10);
  *a = (s16)(s32)(degrees * (65536.0f / 360));
  return a;
}
void rtc_talk_globeVal(rtc_globe *g, f32 radius, s16 *v, s16 *u) {
  hit(11);
  g->mRadius = radius;
  g->mV = *v;
  g->mU = *u;
}
void rtc_talk_getEvIntData(rtc_camera *c, s32 *out, const char *name, s32 def) {
  hit(12);
  assert(c == &camera && def == 0);
  *out = name == (char *)name_data + __builtin_offsetof(rtc_names, mSmoothless)
             ? 0
             : 7;
}
void rtc_talk_getEvFloatData(rtc_camera *c, f32 *out, const char *name,
                             f32 def) {
  hit(13);
  assert(c == &camera && name);
  *out = def;
  if (mutation == 1 && name == (char *)name_data + 0x20)
    *out = 999.9f;
}
rtc_actor *rtc_talk_getEvActor(rtc_camera *c, const char *name,
                               const char *fallback) {
  hit(14);
  assert(c == &camera && fallback);
  return name == (char *)name_data + __builtin_offsetof(rtc_names, mListener)
             ? &actors[0]
             : &actors[1];
}
u32 rtc_talk_getTableEntryHD(rtc_camera *c) {
  hit(15);
  assert(c == &camera);
  return table_actor;
}
s32 rtc_talk_getTableIndexHD(rtc_camera *c) {
  hit(16);
  assert(c == &camera);
  return shot;
}
void rtc_talk_relationalPos(rtc_camera *c, rtc_vec *out, rtc_actor *a,
                            rtc_actor *b, rtc_vec *offset, f32 factor) {
  hit(17);
  assert(c == &camera && a && b && factor == .25f);
  out->x = offset->x + 5;
  out->y = offset->y + 20;
  out->z = offset->z + 10;
}
void rtc_talk_angleValDegrees(s16 *a, f32 degrees) {
  hit(18);
  rtc_talk_angleFromDegrees(a, degrees);
}
void rtc_talk_angleNegate(s16 *a, s16 *out) {
  hit(19);
  *out = (s16) - (unsigned)*a;
}
f64 rtc_talk_angleDegree(s16 *a) {
  hit(20);
  degree_calls++;
  return mutation == 7 ? degree_result : *a * (360.0 / 65536);
}
u32 rtc_talk_lineBGCheck(rtc_camera *c, rtc_vec *a, rtc_vec *b, u32 flags) {
  hit(21);
  assert(c == &camera && a && b && flags == 0x8F);
  line_calls++;
  return mutation == 6 || (mutation == 8 && line_calls < 3);
}
void rtc_talk_angleAddShort(s16 *a, s16 *out, s16 b) {
  hit(22);
  *out = (s16)((unsigned)*a + (unsigned)b);
}
void rtc_vec_mi(const rtc_vec *a, rtc_vec *out, const rtc_vec *b) {
  hit(23);
  out->x = a->x - b->x;
  out->y = a->y - b->y;
  out->z = a->z - b->z;
}
void rtc_vec_pl(const rtc_vec *a, rtc_vec *out, const rtc_vec *b) {
  hit(24);
  out->x = a->x + b->x;
  out->y = a->y + b->y;
  out->z = a->z + b->z;
}
void rtc_vec_ml(const rtc_vec *a, rtc_vec *out, f32 factor) {
  hit(25);
  out->x = a->x * factor;
  out->y = a->y * factor;
  out->z = a->z * factor;
  if (mutation == 4)
    ((rtc_work *)camera.mWork)->mFrameWeight = 7;
  if (mutation == 5)
    ((rtc_work *)camera.mWork)->mTransitionFrames = 1;
}
void rtc_angle_default(s16 *a) {
  hit(26);
  *a = 0;
}
s16 *rtc_angle_value(s16 *a, s16 v) {
  hit(27);
  *a = v;
  return a;
}
void rtc_cSAngle_Val(s16 *a, s16 v) {
  hit(28);
  *a = v;
}
s16 rtc_cSAngle_Inv(const s16 *a) {
  hit(29);
  return (s16)((unsigned)*a + 0x8000);
}
void rtc_cSGlobe_Val(rtc_globe *g, const rtc_vec *v) {
  hit(30);
  rtc_vec copy = *v;
  rtc_talk_globeFromVector(g, &copy);
}
f64 rtc_radiusActorInSight6(rtc_camera *c, rtc_actor *a, rtc_actor *b,
                            rtc_vec *center, rtc_vec *eye, f32 fovy, s16 bank) {
  hit(31);
  assert(c == &camera && a && b && center && eye && bank == 0);
  (void)fovy;
  if (mutation == 2) {
    rtc_work *w = (rtc_work *)c->mWork;
    w->mDirection.mRadius = 123;
  }
  return radius_result;
}
f64 rtc_radiusActorInSight(rtc_camera *c, rtc_actor *a, rtc_actor *b) {
  hit(32);
  assert(c == &camera && a && b);
  if (mutation == 3)
    c->mViewCache.mDirection.mRadius = 123;
  return radius_result;
}
static int actor_index(rtc_actor *a) {
  assert(a >= actors && a < actors + 3);
  return (int)(a - actors);
}
rtc_vec *rtc_positionOf(rtc_camera *c, rtc_vec *out, rtc_actor *a) {
  hit(33);
  assert(c == &camera);
  int index = actor_index(a);
  out->x = index * 100;
  out->y = index * 10;
  out->z = index * 200;
  return out;
}
rtc_vec *rtc_attentionPos(rtc_camera *c, rtc_vec *out, rtc_actor *a) {
  hit(34);
  rtc_positionOf(c, out, a);
  out->y += 30;
  return out;
}
rtc_vec *rtc_eyePos(rtc_camera *c, rtc_vec *out, rtc_actor *a) {
  hit(35);
  rtc_positionOf(c, out, a);
  out->y += 50;
  return out;
}
s16 *rtc_directionOf(rtc_camera *c, s16 *out, rtc_actor *a) {
  hit(36);
  assert(c == &camera);
  *out = (s16)(actor_index(a) * 1000);
  return out;
}
rtc_vec *rtc_positionPntOf(rtc_camera *c, rtc_actor *a) {
  hit(37);
  static rtc_vec pos;
  rtc_positionOf(c, &pos, a);
  return &pos;
}
s16 rtc_targetAngleY(const rtc_vec *a, const rtc_vec *b) {
  hit(38);
  return (s16)(atan2(b->x - a->x, b->z - a->z) * 65536 / 6.283185307179586);
}
void rtc_PSVECAdd(const rtc_vec *a, const rtc_vec *b, rtc_vec *out) {
  hit(39);
  rtc_vec_pl(a, out, b);
}
static rtc_work *reset(void) {
  memset(&camera, 0, sizeof(camera));
  memset(actors, 0, sizeof(actors));
  memset(game, 0, sizeof(game));
  memset(calls, 0, sizeof(calls));
  trace_n = line_calls = collision_calls = degree_calls = 0;
  mutation = 0;
  table_actor = 0;
  radius_result = 0;
  degree_result = 20;
  camera.m11C = 2;
  camera.mpPlayerActor = (u32)&actors[0];
  camera.mpLockonTarget = (u32)&actors[1];
  camera.m368 = 2;
  camera.mViewCache.mDirection.mRadius = 120;
  camera.mViewCache.mFovy = 50;
  camera.mViewCache.mEye.y = 100;
  rtc_work *w = (rtc_work *)camera.mWork;
  w->mpListener = (u32)&actors[0];
  w->mpSpeaker = w->mpPreviousSpeaker = (u32)&actors[1];
  w->mPreviousShotType = -1;
  w->mSide = 0;
  w->mTransitionFrames = 10;
  w->mRadius = w->mDirection.mRadius = 100;
  w->mFovy = 55;
  w->mLatitude = 15;
  w->mLongitude = 999.9f;
  w->mActorDirection.mRadius = 200;
  w->mWeightSum = 55;
  return w;
}
static int expected_fovy(int type) {
  switch (type) {
  case 11:
  case 12:
  case 29:
    return 55;
  case 13:
  case 20:
  case 21:
  case 28:
    return 60;
  case 14:
  case 15:
  case 16:
  case 17:
  case 18:
  case 19:
  case 26:
  case 27:
    return 50;
  case 22:
  case 23:
  case 24:
  case 25:
    return 45;
  case 30:
  case 31:
    return 65;
  default:
    return 0;
  }
}
int main(void) {
  unsigned cases = 0;
  *(s16 *)angle_data = 0;
  *(s16 *)(angle_data + 4) = 0x4000;
  *(s16 *)(angle_data + 8) = (s16)0xC000;
  f32 *styles = (f32 *)style_data;
  styles[2 + 5] = 20;
  styles[2 + 10] = 100;
  styles[2 + 18] = -50;
  styles[2 + 25] = 55;
  styles[2 + 24] = 20;
  styles[2 + 15] = 15;
  styles[2 + 19] = 50;
  styles[2 + 3] = 1;
  const int types[] = {0,  11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22,
                       23, 24, 25, 26, 27, 28, 29, 30, 31, 1,  32, -1};
  for (unsigned t = 0; t < 25; t++)
    for (unsigned first = 0; first < 2; first++)
      for (unsigned side = 0; side < 2; side++)
        for (unsigned done = 0; done < 2; done++) {
          rtc_work *w = reset();
          shot = types[t];
          w->mSide = side;
          w->mShotFrames = first ? 0 : 3;
          w->mPreviousShotType = shot;
          camera.m100 = done;
          s32 expected_frames = w->mShotFrames + 1;
          const int ret = randomizer_talk_camera((u32)&camera, 0);
          assert(w->mShotFrames == expected_frames &&
                 w->mPreviousShotType == shot && w->mShotType == shot);
          if (shot == 0) {
            assert(ret == (int)done);
            if (!done)
              assert(w->mWeightSum == 55 - (10 - (first ? 0 : 3)));
          } else
            assert(ret == 1);
          int fovy = expected_fovy(shot);
          if (fovy &&
              (!((shot == 11 || shot == 12 || shot == 30 || shot == 31) &&
                 !first)))
            assert(camera.mViewCache.mFovy == fovy);
          if (shot == 14 || shot == 15 || shot == 16 || shot == 17 ||
              shot == 20 || shot == 21 || shot == 22 || shot == 23 ||
              shot == 29)
            assert((actors[0].actor_status | actors[1].actor_status) &
                   0x01000000);
          cases++;
        }
  for (unsigned event = 0; event < 2; event++)
    for (unsigned profile = 0; profile < 3; profile++)
      for (unsigned mut = 0; mut < 9; mut++) {
        rtc_work *w = reset();
        shot = 0;
        camera.m11C = 0;
        *(s32 *)(game + 0x52E4) = event;
        actors[1].name = profile == 1 ? 0xA5 : profile == 2 ? 0xB6 : 0;
        mutation = mut;
        radius_result = .75;
        degree_result = 20;
        assert(randomizer_talk_camera((u32)&camera, 0) == 0);
        assert(w->mTransitionFrames == (mut == 5 ? 1 : 10) &&
               w->mTag == 0x54414C4B && w->mShotFrames == 1 &&
               w->mPreviousShotType == 0);
        assert(calls[15] == 1 && calls[16] == 1 && calls[31] == 1);
        if (event)
          assert(calls[12] == 2 && calls[13] == 4 && calls[14] == 2);
        else
          assert(!calls[12] && !calls[13] && !calls[14]);
        if (mut == 2)
          assert(w->mDirection.mRadius == 123.75f);
        cases++;
      }
  for (unsigned mode = 0; mode < 4; mode++) {
    rtc_work *w = reset();
    shot = 0;
    if (mode == 0)
      camera.mpPlayerActor = camera.mpLockonTarget;
    if (mode == 1)
      camera.mpPlayerActor = 0;
    if (mode == 2)
      camera.mpLockonTarget = 0;
    if (mode == 3)
      table_actor = (u32)&actors[0];
    assert(randomizer_talk_camera((u32)&camera, 0) == 0);
    assert(camera.m100 == 1 && camera.m101 == 1 && camera.m102 == 1 &&
           !calls[16] && w->mShotFrames == 0);
    cases++;
  }
  for (unsigned mut = 4; mut <= 5; mut++) {
    rtc_work *w = reset();
    shot = 0;
    w->mPreviousShotType = 0;
    w->mShotFrames = 3;
    mutation = mut;
    assert(randomizer_talk_camera((u32)&camera, 0) == 0);
    assert(w->mWeightSum == 48);
    if (mut == 5)
      assert(camera.m100 == 1 && camera.m101 == 1 && camera.m102 == 1);
    cases++;
  }
  const f64 radii[] = {-INFINITY, -.5, -0.0, 0, 0x1p-1074, .75, INFINITY, NAN};
  for (unsigned i = 0; i < 8; i++) {
    rtc_work *w = reset();
    shot = 28;
    w->mPreviousShotType = 28;
    mutation = 3;
    radius_result = radii[i];
    const unsigned old_globes = calls[8];
    assert(randomizer_talk_camera((u32)&camera, 0) == 1);
    const f32 expected = radii[i] > 0 ? (f32)(123.0 + radii[i]) : 123;
    assert(camera.mViewCache.mDirection.mRadius == expected);
    assert(calls[8] - old_globes == (radii[i] > 0 ? 2 : 1));
    cases++;
  }
  const f64 degree_edges[] = {9.999999999999998, 10.0, 10.000000000000002, NAN};
  for (unsigned i = 0; i < 4; i++) {
    rtc_work *w = reset();
    (void)w;
    shot = 0;
    camera.m11C = 0;
    mutation = 7;
    degree_result = degree_edges[i];
    assert(randomizer_talk_camera((u32)&camera, 0) == 0);
    if (i == 0)
      assert(degree_calls == 36 && line_calls == 0);
    else
      assert(degree_calls == 1 && line_calls >= 3);
    cases++;
  }
  for (unsigned t = 0; t < 2; t++) {
    rtc_work *w = reset();
    (void)w;
    shot = t ? 13 : 0;
    camera.m11C = 0;
    mutation = 6;
    assert(randomizer_talk_camera((u32)&camera, 0) == (s32)t);
    assert(line_calls > 0 && degree_calls == 36);
    if (t)
      assert(line_calls >= 18);
    cases++;
  }
  for (unsigned i = 0; i < 65536; i++) {
    rtc_work *w = reset();
    shot = 32;
    w->mPreviousShotType = 32;
    w->mShotFrames = (s32)(0x7FFF8000u + i);
    const s32 expected = (s32)((uint32_t)w->mShotFrames + 1);
    assert(randomizer_talk_camera((u32)&camera, 0) == 1 &&
           w->mShotFrames == expected);
    cases++;
  }
  printf("%u complete talk camera cases passed\n", cases);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            source = directory / 'test.c'
            source.write_text(code)
            subprocess.run(['clang', '-O2', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror', '-I', str(Path(__file__).parent), str(source), '-lm', '-o', str(directory / 'test')], check=True)
            subprocess.run([str(directory / 'test')], check=True)


if __name__ == '__main__':
    unittest.main()
