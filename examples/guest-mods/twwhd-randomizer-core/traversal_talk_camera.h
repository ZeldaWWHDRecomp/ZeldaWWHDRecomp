/* Complete public CC0 dCamera_c::talktoCamera, a874e1a9.
 * MIT tweaks.cpp changes only initial transition frames20->10. */
#pragma once
/* Layouts are guest32; native callbacks retain their original context/result
 * slots. All helpers below are local portions of the same complete consumer. */
#include <stdbool.h>
typedef struct {
  f32 x, y, z;
} rtc_vec;
typedef struct {
  f32 mRadius;
  s16 mV, mU;
} rtc_globe;
typedef struct {
  u8 pad[8];
  u16 name;
  u8 padA[0x2E0 - 10];
  u32 actor_status;
} rtc_actor;
typedef struct {
  rtc_globe mDirection;
  rtc_vec mCenter, mEye;
  s16 mBank;
  u8 pad[2];
  f32 mFovy;
} rtc_view;
typedef struct {
  u32 mTag;
  rtc_vec mCenter, mEye, mShotCenter;
  rtc_globe mDirection, mActorDirection;
  s32 mSide, mShotType, mPreviousShotType, mShotFrames, mTransitionFrames;
  f32 mWeightSum, mFrameWeight;
  u8 pad[4];
  f32 mRadius, mFovy, mLongitude, mLatitude;
  u32 mpListener, mpSpeaker, mpPreviousSpeaker;
  s16 mSmoothless, mMode;
} rtc_work;
typedef struct {
  u8 pad[0x34];
  s16 mBank;
  u8 pad36[2];
  f32 mFovy;
  rtc_view mViewCache;
  u8 pad64[0x100 - 0x64];
  u8 m100, m101, m102, pad103;
  u8 pad104[0x11C - 0x104];
  u32 m11C;
  u8 pad120[8];
  u32 mpPlayerActor, mpLockonTarget;
  u8 pad130[0x36C - 0x130];
  f32 m368;
  u8 pad370[0xC];
  u8 mWork[sizeof(rtc_work) + 8];
} rtc_camera;
typedef struct {
  char mSpeaker[8], mMode[8], mRadius[8], mFovy[8], mLongitude[12],
      mSmoothless[12], mLatitude[12], mListener[12], mStarter[12],
      mTalkPartner[16];
} rtc_names;
#define rtc_talkAngleZero WWHD_GAME_DATA(0x101FF354, s16)
#define rtc_talkAngle90 WWHD_GAME_DATA(0x101FF358, s16)
#define rtc_talkAngle270 WWHD_GAME_DATA(0x101FF35C, s16)
#define rtc_talkEventNames                                                     \
  (*(rtc_names *)(u32) & WWHD_GAME_DATA(0x1004ACDC, u8))
enum {
  RTC_fpcNm_SHIP_e = 0xA5,
  RTC_fpcNm_KANBAN_e = 0xB6,
  RTC_TALK_WORK_TAG = 0x54414C4B,
  RTC_fopAcStts_NODRAW_e = 0x01000000
};
enum {
  RTC_TALK_PAIR = 0,
  RTC_TALK_CLOSEUP_A = 11,
  RTC_TALK_CLOSEUP_B,
  RTC_TALK_WIDE,
  RTC_TALK_EYE_A,
  RTC_TALK_EYE_B,
  RTC_TALK_SHOULDER_NEAR_A,
  RTC_TALK_SHOULDER_NEAR_B,
  RTC_TALK_SIDE_A,
  RTC_TALK_SIDE_B,
  RTC_TALK_SHOULDER_A,
  RTC_TALK_SHOULDER_B,
  RTC_TALK_SHOULDER_LOW_A,
  RTC_TALK_SHOULDER_LOW_B,
  RTC_TALK_SIDE_HIGH_A,
  RTC_TALK_SIDE_HIGH_B,
  RTC_TALK_SIDE_ATTN_A,
  RTC_TALK_SIDE_ATTN_B,
  RTC_TALK_MIDPOINT,
  RTC_TALK_SHOULDER_ATTN,
  RTC_TALK_CLOSEUP_WIDE_A,
  RTC_TALK_CLOSEUP_WIDE_B
};
#if defined(__powerpc__)
_Static_assert(__builtin_offsetof(rtc_actor, actor_status) == 0x2E0,
               "actor status");
_Static_assert(sizeof(rtc_work) == 0x78, "talk work");
_Static_assert(__builtin_offsetof(rtc_camera, mWork) == 0x37C, "camera work");
_Static_assert(__builtin_offsetof(rtc_camera, mViewCache) == 0x3C,
               "camera cache");
_Static_assert(__builtin_offsetof(rtc_camera, m368) == 0x36C, "camera height");
#endif
WWHD_GAME_FUNC(0x025200D4, u32, rtc_game, (void));
static void *rtc_ccs(void) { return (void *)(rtc_game() + 0x26A4); }
static s32 rtc_event_camera(void) {
  return *(volatile s32 *)(rtc_game() + 0x52E4);
}
static f32 rtc_camParamVal(s32 style, s32 index) {
  return ((volatile f32 *)&WWHD_GAME_DATA(
      0x1004487C, u8))[(u32)style * 33 + 2 + (u32)index];
}
static u16 rtc_fpcM_GetName(rtc_actor *a) { return a->name; }
static void rtc_copy(rtc_vec *d, const rtc_vec *s) {
  __builtin_memcpy(d, s, 12);
}
static f32 rtc_madd(f32 a, f32 b, f32 c) {
#pragma clang fp contract(fast)
  return a * b + c;
}
WWHD_GAME_FUNC(0x0200658C, s16 *, rtc_talk_angleFromShort, (s16 *, s16));
WWHD_GAME_FUNC(0x020068B0, void, rtc_talk_angleSubtract,
               (const void *, s16 *, const void *));
WWHD_GAME_FUNC(0x0200693C, void, rtc_talk_angleMultiply,
               (const void *, s16 *, f32));
WWHD_GAME_FUNC(0x02006894, void, rtc_talk_angleAdd,
               (const void *, s16 *, const void *));
WWHD_GAME_FUNC(0x02006838, f64, rtc_talk_angleCos, (const void *));
WWHD_GAME_FUNC(0x025187D8, u32, rtc_talk_ChkCamera,
               (void *, rtc_vec *, rtc_vec *, f32, rtc_actor *, rtc_actor *));
WWHD_GAME_FUNC(0x020073AC, void, rtc_talk_globeXyz, (rtc_globe *, rtc_vec *));
WWHD_GAME_FUNC(0x02007324, void, rtc_talk_globeFromVector,
               (rtc_globe *, rtc_vec *));
WWHD_GAME_FUNC(0x020066C0, s16 *, rtc_talk_angleFromDegrees, (s16 *, f32));
WWHD_GAME_FUNC(0x020071E0, void, rtc_talk_globeVal,
               (rtc_globe *, f32, s16 *, s16 *));
WWHD_GAME_FUNC(0x02530634, void, rtc_talk_getEvIntData,
               (rtc_camera *, s32 *, const char *, s32));
WWHD_GAME_FUNC(0x0253072C, void, rtc_talk_getEvFloatData,
               (rtc_camera *, f32 *, const char *, f32));
WWHD_GAME_FUNC(0x02530CB4, rtc_actor *, rtc_talk_getEvActor,
               (rtc_camera *, const char *, const char *));
WWHD_GAME_FUNC(0x02502364, u32, rtc_talk_getTableEntryHD, (rtc_camera *));
WWHD_GAME_FUNC(0x025023C4, s32, rtc_talk_getTableIndexHD, (rtc_camera *));
WWHD_GAME_FUNC(0x02508804, void, rtc_talk_relationalPos,
               (rtc_camera *, rtc_vec *, rtc_actor *, rtc_actor *, rtc_vec *,
                f32));
WWHD_GAME_FUNC(0x02006694, void, rtc_talk_angleValDegrees, (s16 *, f32));
WWHD_GAME_FUNC(0x02006880, void, rtc_talk_angleNegate, (s16 *, s16 *));
WWHD_GAME_FUNC(0x02006720, f64, rtc_talk_angleDegree, (s16 *));
WWHD_GAME_FUNC(0x024FCBE8, u32, rtc_talk_lineBGCheck,
               (rtc_camera *, rtc_vec *, rtc_vec *, u32));
WWHD_GAME_FUNC(0x020068F4, void, rtc_talk_angleAddShort, (s16 *, s16 *, s16));
WWHD_GAME_FUNC(0x0201ADE0, void, rtc_vec_mi,
               (const rtc_vec *a, rtc_vec *out, const rtc_vec *b));
WWHD_GAME_FUNC(0x0201AD78, void, rtc_vec_pl,
               (const rtc_vec *a, rtc_vec *out, const rtc_vec *b));
WWHD_GAME_FUNC(0x0201AE48, void, rtc_vec_ml,
               (const rtc_vec *a, rtc_vec *out, f32 scale));
WWHD_GAME_FUNC(0x020065FC, void, rtc_angle_default, (s16 * a));
WWHD_GAME_FUNC(0x0200658C, s16 *, rtc_angle_value, (s16 * a, s16 v));
WWHD_GAME_FUNC(0x02006584, void, rtc_cSAngle_Val, (s16 * a, s16 v));
WWHD_GAME_FUNC(0x02006804, s16, rtc_cSAngle_Inv, (const s16 *a));
WWHD_GAME_FUNC(0x020072E0, void, rtc_cSGlobe_Val,
               (rtc_globe * g, const rtc_vec *v));
WWHD_GAME_FUNC(0x024FC780, f64, rtc_radiusActorInSight6,
               (rtc_camera * c, rtc_actor *a, rtc_actor *b, rtc_vec *center,
                rtc_vec *eye, f32 fovy, s16 bank));
WWHD_GAME_FUNC(0x024FCBD4, f64, rtc_radiusActorInSight,
               (rtc_camera * c, rtc_actor *a, rtc_actor *b));
WWHD_GAME_FUNC(0x024F8D5C, rtc_vec *, rtc_positionOf,
               (rtc_camera * c, rtc_vec *out, rtc_actor *a));
WWHD_GAME_FUNC(0x024F8F3C, rtc_vec *, rtc_attentionPos,
               (rtc_camera * c, rtc_vec *out, rtc_actor *a));
WWHD_GAME_FUNC(0x024FC0B0, rtc_vec *, rtc_eyePos,
               (rtc_camera * c, rtc_vec *out, rtc_actor *a));
WWHD_GAME_FUNC(0x024F8F30, s16 *, rtc_directionOf,
               (rtc_camera * c, s16 *out, rtc_actor *a));
WWHD_GAME_FUNC(0x02508A58, rtc_vec *, rtc_positionPntOf,
               (rtc_camera * c, rtc_actor *a));
WWHD_GAME_FUNC(0x0200F93C, s16, rtc_targetAngleY,
               (const rtc_vec *a, const rtc_vec *b));
WWHD_GAME_FUNC(0x028E8D88, void, rtc_PSVECAdd,
               (const rtc_vec *a, const rtc_vec *b, rtc_vec *out));
static inline void rtc_copyTalkFloatBits(void *dst, const void *src) {
  __builtin_memcpy(dst, src, 4);
}
static inline s16 rtc_talkAngleShortValue(s16 *tmp, s16 v) {
  return *rtc_talk_angleFromShort(tmp, v);
}

static inline bool rtc_chkCamera(rtc_vec *a, rtc_vec *b, f32 r, rtc_actor *a1,
                                 rtc_actor *a2) {
  return rtc_talk_ChkCamera(rtc_ccs(), a, b, r, a1, a2); /* dCcS::ChkCamera */
}
static inline void rtc_cpXyz(rtc_vec *d, const rtc_vec *s) { /* lfs/stfs copy */
  rtc_copyTalkFloatBits(&d->x, &s->x);
  rtc_copyTalkFloatBits(&d->y, &s->y);
  rtc_copyTalkFloatBits(&d->z, &s->z);
}
/* c->mViewCache.mEye = mCenter + mDirection.Xyz() */
static void rtc_talkEye(rtc_camera *c) {
  rtc_vec x_storage, *x = &x_storage;
  rtc_vec e_storage, *e = &e_storage;
  rtc_talk_globeXyz(&c->mViewCache.mDirection, x);
  rtc_vec_pl(&c->mViewCache.mCenter, e, x);
  rtc_copy(&c->mViewCache.mEye, e);
}
static inline void rtc_flagsDone(rtc_camera *c) {
  c->m102 = 1;
  c->m101 = 1;
  c->m100 = 1;
}
/* m10 = m04 + m28.Xyz(), then pushed out by rtc_radiusActorInSight */
static void rtc_talkFinish(rtc_camera *c, rtc_work *work, rtc_actor *a1,
                           rtc_actor *a2) {
  rtc_vec x_storage, *x = &x_storage;
  rtc_vec e_storage, *e = &e_storage;
  s32 n = work->mTransitionFrames;
  work->mWeightSum =
      (f32)((s32)((unsigned int)n * ((unsigned int)n + 1u)) >> 1);
  rtc_talk_globeXyz(&work->mDirection, x);
  rtc_vec_pl(&work->mCenter, e, x);
  rtc_copy(&work->mEye, e);
  f64 r = rtc_radiusActorInSight6(c, a1, a2, &work->mCenter, &work->mEye,
                                  work->mFovy, 0);
  if (r > 0.0f) {
    rtc_vec x2_storage, *x2 = &x2_storage;
    rtc_vec e2_storage, *e2 = &e2_storage;
    work->mDirection.mRadius = (f32)((f64)work->mDirection.mRadius + r);
    rtc_talk_globeXyz(&work->mDirection, x2);
    rtc_vec_pl(&work->mCenter, e2, x2);
    rtc_copy(&work->mEye, e2);
  }
}

/* the over-the-shoulder shots (types 0x10/0x11, 0x14/0x15, 0x16/0x17, 0x1D):
 * the centre at the speaker, the direction from the listener; first frame: the
 * radius and the height offset */
static void rtc_talkShoulder(rtc_camera *c, rtc_work *work, rtc_actor *listener,
                             rtc_actor *speaker, f32 radius, f32 dy, f32 fovy,
                             bool attnFirst) {
  listener->actor_status = listener->actor_status | RTC_fopAcStts_NODRAW_e;
  rtc_vec pos_storage, *pos = &pos_storage;
  rtc_positionOf(c, pos, speaker);
  rtc_copy(&c->mViewCache.mCenter, pos);
  if (work->mShotFrames == 0) {
    rtc_vec a_storage, *a = &a_storage;
    rtc_vec b_storage, *b = &b_storage;
    rtc_vec d_storage, *d = &d_storage;
    rtc_vec sp_storage, *sp = &sp_storage;
    rtc_positionOf(c, a, listener);
    rtc_positionOf(c, b, speaker);
    rtc_vec_mi(a, d, b);
    rtc_cSGlobe_Val(&c->mViewCache.mDirection, d);
    c->mViewCache.mDirection.mRadius = radius;
    if (attnFirst)
      rtc_attentionPos(c, d, speaker);
    else
      rtc_eyePos(c, d, speaker);
    rtc_positionOf(c, sp, speaker);
    work->mShotCenter.y = (d->y - dy) - sp->y;
    rtc_flagsDone(c);
  }
  rtc_vec pos2_storage, *pos2 = &pos2_storage;
  rtc_positionOf(c, pos2, speaker);
  c->mViewCache.mCenter.y = pos2->y + work->mShotCenter.y;
  rtc_talkEye(c);
  c->mViewCache.mFovy = fovy;
}

/* the side shots (types 0x12/0x13, 0x18/0x19, 0x1A/0x1B): the centre from an
 * offset globe around B rotated by the A -> B direction (first frame), then a
 * fixed globe around it */
static void rtc_talkSide(rtc_camera *c, rtc_work *work, rtc_actor *A,
                         rtc_actor *B, bool side, f32 ofsY, bool attnA, f32 u0,
                         f32 u1, f32 radius, f32 lat, f32 fovy) {
  if (work->mShotFrames == 0) {
    rtc_vec o_storage, *o = &o_storage;
    rtc_vec ea_storage, *ea = &ea_storage;
    rtc_vec ab_storage, *ab = &ab_storage;
    rtc_vec d_storage, *d = &d_storage;
    rtc_vec eb_storage, *eb = &eb_storage;
    rtc_vec x_storage, *x = &x_storage;
    rtc_vec s_storage, *s = &s_storage;
    rtc_globe gb_storage, *gb = &gb_storage;
    rtc_globe g30_storage, *g30 = &g30_storage;
    s16 t_storage, *t = &t_storage;
    s16 tmp_storage, *tmp = &tmp_storage;
    o->x = 0.0f;
    o->z = 20.0f;
    o->y = ofsY;
    rtc_flagsDone(c);
    if (attnA)
      rtc_attentionPos(c, ea, A);
    else
      rtc_eyePos(c, ea, A);
    rtc_attentionPos(c, ab, B);
    rtc_vec_mi(ea, d, ab);
    rtc_talk_globeFromVector(gb, d);
    rtc_talk_globeFromVector(g30, o);
    rtc_talk_angleAdd(&g30->mU, t, &gb->mU);
    g30->mU = rtc_talkAngleShortValue(tmp, *t);
    rtc_eyePos(c, eb, B);
    rtc_talk_globeXyz(g30, x);
    rtc_vec_pl(eb, s, x);
    rtc_cpXyz(&work->mShotCenter, s);
    rtc_copyTalkFloatBits(&c->mViewCache.mCenter.y, &s->y);
    rtc_copyTalkFloatBits(&c->mViewCache.mCenter.x, &s->x);
    rtc_copyTalkFloatBits(&c->mViewCache.mCenter.z, &work->mShotCenter.z);
  } else {
    rtc_copyTalkFloatBits(&c->mViewCache.mCenter.y, &work->mShotCenter.y);
    rtc_copyTalkFloatBits(&c->mViewCache.mCenter.x, &work->mShotCenter.x);
    rtc_copyTalkFloatBits(&c->mViewCache.mCenter.z, &work->mShotCenter.z);
  }
  s16 a_storage, *a = &a_storage;
  s16 dir_storage, *dir = &dir_storage;
  s16 u_storage, *u = &u_storage;
  s16 l_storage, *l = &l_storage;
  rtc_talk_angleFromDegrees(a, side ? u1 : u0);
  rtc_directionOf(c, dir, B);
  rtc_talk_angleAdd(a, u, dir);
  if (lat == 0.0f) {
    rtc_talk_globeVal(&c->mViewCache.mDirection, radius,
                      (s16 *)(u32)&rtc_talkAngleZero, u);
  } else {
    s16 *lp = rtc_talk_angleFromDegrees(l, lat);
    rtc_talk_globeVal(&c->mViewCache.mDirection, radius, lp, u);
  }
  rtc_talkEye(c);
  c->mViewCache.mFovy = fovy;
}

/* 02508A60 */
// HD: conversation shot selection uses the talk table, shoulder/side/close-up
// shots and collision avoidance. The corresponding GameCube method is still a
// nonmatching stub.
WWHD_REPLACE(0x02508A60, s32, randomizer_talk_camera, (u32 self, s32 style)) {
#pragma clang fp contract(off)
  rtc_camera *c = (rtc_camera *)self;
  rtc_work *work = (rtc_work *)c->mWork;
  f32 centerOffsetY = rtc_camParamVal(style, 5);
  f32 centerOffsetZ = rtc_camParamVal(style, 0);
  f32 defaultRadius = rtc_camParamVal(style, 10);
  f32 minLatitude = rtc_camParamVal(style, 18);
  f32 defaultFovy = rtc_camParamVal(style, 25);
  f32 centerOffsetX = rtc_camParamVal(style, 1);
  f32 minLongitude = rtc_camParamVal(style, 24);
  f32 defaultLatitude = rtc_camParamVal(style, 15);
  f32 maxLatitude = rtc_camParamVal(style, 19);
  f32 latitudeScale = rtc_camParamVal(style, 3);
  bool ret = true;
  if (c->m11C == 0) {
    work->mShotFrames = 0;
    work->mSide = -1;
    work->mTransitionFrames = 10;
    work->mShotType = 0;
    work->mPreviousShotType = -1;
    work->mLongitude = 999.9f;
    work->mLatitude = defaultLatitude;
    work->mTag = RTC_TALK_WORK_TAG; /* 'TALK' */
    if (rtc_event_camera() == 0) {
      work->mSmoothless = 0;
      work->mMode = 0;
      work->mRadius = defaultRadius;
      work->mFovy = defaultFovy;
      work->mpListener = (u32)(((rtc_actor *)(u32)c->mpPlayerActor));
      rtc_actor *t = ((rtc_actor *)(u32)c->mpLockonTarget);
      work->mpSpeaker = (u32)(t);
      work->mpPreviousSpeaker = (u32)(t);
    } else {
      s32 iv_storage, *iv = &iv_storage;
      rtc_talk_getEvIntData(c, iv, rtc_talkEventNames.mSmoothless,
                            0); /* getEvIntData("Smoothless") */
      work->mSmoothless = (s16)(s32)*iv;
      rtc_talk_getEvIntData(c, iv, rtc_talkEventNames.mMode, 0); /* "Mode" */
      work->mMode = (s16)(s32)*iv;
      rtc_talk_getEvFloatData(c, &work->mRadius, rtc_talkEventNames.mRadius,
                              defaultRadius); /* getEvFloatData("Radius") */
      rtc_talk_getEvFloatData(c, &work->mLongitude,
                              rtc_talkEventNames.mLongitude,
                              999.9f); /* "Longitude" */
      rtc_talk_getEvFloatData(c, &work->mFovy, rtc_talkEventNames.mFovy,
                              defaultFovy); /* "Fovy" */
      rtc_talk_getEvFloatData(c, &work->mLatitude, rtc_talkEventNames.mLatitude,
                              defaultLatitude); /* "Latitude" */
      work->mpListener = (u32)(rtc_talk_getEvActor(
          c, rtc_talkEventNames.mListener,
          rtc_talkEventNames
              .mStarter)); /* getEvActor("Listener", "@STARTER") */
      rtc_actor *s = rtc_talk_getEvActor(
          c, rtc_talkEventNames.mSpeaker,
          rtc_talkEventNames.mTalkPartner); /* ("Speaker", "@TALKPARTNER") */
      work->mpSpeaker = (u32)(s);
      work->mpPreviousSpeaker = (u32)(s);
    }
  }
  rtc_actor *a1;
  rtc_actor *a2 =
      (rtc_actor *)(u32)rtc_talk_getTableEntryHD(c); // HD: talk table entry
  bool same;
  if (a2 != 0) {
    a1 = ((rtc_actor *)(u32)work->mpListener);
    same = a1 == a2;
  } else if (rtc_event_camera() != 0) {
    u32 c11C = c->m11C;
    a1 = ((rtc_actor *)(u32)work->mpListener);
    if (c11C != 0)
      a2 = rtc_talk_getEvActor(c, rtc_talkEventNames.mSpeaker,
                               rtc_talkEventNames.mTalkPartner);
    else
      a2 = ((rtc_actor *)(u32)work->mpSpeaker);
    same = a1 == a2;
  } else {
    a2 = ((rtc_actor *)(u32)c->mpLockonTarget);
    a1 = ((rtc_actor *)(u32)c->mpPlayerActor);
    same = a1 == a2;
  }
  if (same || a1 == 0 || a2 == 0) {
    rtc_flagsDone(c);
    return false;
  }
  if (((rtc_actor *)(u32)work->mpPreviousSpeaker) != a2) {
    c->m11C = 0;
    c->m100 = 0;
    work->mShotFrames = 0;
    work->mpPreviousSpeaker = (u32)(a2);
  }
  {
    s32 t = rtc_talk_getTableIndexHD(c); // HD: talk shot type
    s32 prev = work->mPreviousShotType;
    work->mShotType = t;
    if (t != prev)
      work->mShotFrames = 0;
  }
  s16 latHi_storage, *latHi = &latHi_storage;
  s16 latLo_storage, *latLo = &latLo_storage;
  s16 lonMax_storage, *lonMax = &lonMax_storage;
  s16 lonMin_storage, *lonMin = &lonMin_storage;
  s16 tmp_storage, *tmp = &tmp_storage;
  rtc_talk_angleFromDegrees(latHi, maxLatitude);
  rtc_talk_angleFromDegrees(latLo, minLatitude);
  rtc_talk_angleFromDegrees(lonMax, 180.0f - minLongitude);
  rtc_talk_angleFromDegrees(lonMin, minLongitude);
  u32 c11C = c->m11C;
  if (c11C == 0 ||
      (a2 != 0 && rtc_fpcM_GetName(a2) == RTC_fpcNm_SHIP_e && c11C <= 1)) {
    rtc_vec ofs_storage, *ofs = &ofs_storage;
    rtc_vec ep_storage, *ep = &ep_storage;
    rtc_vec at1_storage, *at1 = &at1_storage;
    rtc_vec at2_storage, *at2 = &at2_storage;
    rtc_vec r_storage, *r = &r_storage;
    ofs->x = centerOffsetX;
    ofs->y = centerOffsetY;
    ofs->z = centerOffsetZ;
    rtc_attentionPos(c, at1, a1);
    rtc_eyePos(c, ep, a1);
    rtc_copyTalkFloatBits(&at1->y, &ep->y);
    rtc_attentionPos(c, at2, a2);
    rtc_eyePos(c, ep, a2);
    rtc_copyTalkFloatBits(&at2->y, &ep->y);
    rtc_talk_relationalPos(c, r, a1, a2, ofs,
                           0.25f); /* relationalPos(a1, a2, ofs, 0.25) */
    rtc_copy(&work->mCenter, r);
    s16 lon_storage, *lon = &lon_storage;
    rtc_angle_default(lon);
    if (work->mLongitude != 999.9f) {
      rtc_talk_angleValDegrees(lon, (f32)work->mLongitude);
    } else {
      s16 ty = rtc_targetAngleY(&work->mCenter, &c->mViewCache.mEye);
      rtc_cSAngle_Val(lon, ty);
    }
    rtc_globe g_storage, *g = &g_storage;
    {
      rtc_vec d_storage, *d = &d_storage;
      rtc_copyTalkFloatBits(&work->mDirection.mRadius,
                            &c->mViewCache.mDirection.mRadius);
      rtc_copyTalkFloatBits(&work->mDirection.mV, &c->mViewCache.mDirection.mV);
      rtc_vec_mi(at1, d, at2);
      rtc_talk_globeFromVector(g, d);
      rtc_copyTalkFloatBits(&work->mActorDirection.mRadius, &g->mRadius);
      rtc_copyTalkFloatBits(&work->mDirection.mRadius, &work->mRadius);
      rtc_copyTalkFloatBits(&work->mActorDirection.mV, &g->mV);
      /* the copy keeps the radius >= 88 on the stack (g is used below as
       * work->mActorDirection's twin) */
      if (g->mRadius < 88.0f)
        g->mRadius = 88.0f;
      if (a2 != 0 && rtc_fpcM_GetName(a2) == RTC_fpcNm_SHIP_e) {
        /* ship: look past the ship's bow side */
        s16 tA_storage, *tA = &tA_storage;
        s16 dir_storage, *dir = &dir_storage;
        s16 rel_storage, *rel = &rel_storage;
        s16 side_storage, *side = &side_storage;
        s16 t2_storage, *t2 = &t2_storage;
        s16 ty = rtc_targetAngleY(rtc_positionPntOf(c, a2), at2);
        rtc_angle_value(tA, ty);
        rtc_directionOf(c, dir, a2);
        rtc_talk_angleSubtract(dir, rel, tA);
        rtc_angle_default(side);
        s16 *sp;
        if ((s16)*rel < rtc_talkAngleZero)
          sp = rtc_talk_angleFromDegrees(t2, -30.0f);
        else
          sp = rtc_talk_angleFromDegrees(t2, 30.0f);
        *side = (s16)*sp;
        rtc_talk_angleAdd(&g->mU, dir, side);
        work->mDirection.mU = rtc_talkAngleShortValue(tmp, *dir);
        s16 *lp = rtc_talk_angleFromDegrees(t2, defaultLatitude);
        work->mDirection.mV = rtc_talkAngleShortValue(tmp, *lp);
        rtc_talkFinish(c, work, a1, a2);
      } else if (a2 != 0 && rtc_fpcM_GetName(a2) == RTC_fpcNm_KANBAN_e) {
        s16 dir_storage, *dir = &dir_storage;
        s16 tf0_storage, *tf0 = &tf0_storage;
        s16 tee_storage, *tee = &tee_storage;
        s16 t2_storage, *t2 = &t2_storage;
        rtc_directionOf(c, dir, a2);
        g->mU = rtc_talkAngleShortValue(tmp, *dir);
        rtc_talk_angleSubtract(lon, tf0, &g->mU);
        rtc_talk_angleMultiply(tf0, tee, 0.25f);
        rtc_talk_angleAdd(&g->mU, dir, tee);
        work->mDirection.mU = rtc_talkAngleShortValue(tmp, *dir);
        s16 *lp = rtc_talk_angleFromDegrees(t2, (f32)work->mLatitude);
        work->mDirection.mV = rtc_talkAngleShortValue(tmp, *lp);
        rtc_talkFinish(c, work, a1, a2);
      } else {
        s16 rel_storage, *rel = &rel_storage;
        s16 neg_storage, *neg = &neg_storage;
        s16 t2c_storage, *t2c = &t2c_storage;
        s16 tf4_storage, *tf4 = &tf4_storage;
        s16 tf2_storage, *tf2 = &tf2_storage;
        s16 lat15_storage, *lat15 = &lat15_storage;
        s16 step_storage, *step = &step_storage;
        s16 t14_storage, *t14 = &t14_storage;
        rtc_talk_angleSubtract(lon, rel, &g->mU);
        s16 r = *rel;
        if (r > (s16)*lonMax) {
          r = *lonMax;
          *rel = r;
        }
        s16 zero = rtc_talkAngleZero;
        if (r > zero && r < (s16)*lonMin)
          *rel = (s16)*lonMin;
        rtc_talk_angleNegate(lonMax, t2c); /* -lonMax */
        r = *rel;
        if (r < (s16)*t2c) {
          rtc_talk_angleNegate(lonMax, neg);
          r = *neg;
          *rel = r;
        }
        if (r < zero) {
          rtc_talk_angleNegate(lonMin, t2c); /* -lonMin */
          if ((s16)*rel > (s16)*t2c) {
            rtc_talk_angleNegate(lonMin, neg);
            *rel = (s16)*neg;
          }
        }
        rtc_talk_angleAdd(&g->mU, t2c, rel);
        work->mDirection.mU = rtc_talkAngleShortValue(tmp, *t2c);
        rtc_talk_angleSubtract(&g->mU, tf4, lon);
        f32 cosine_weight = rtc_talk_angleCos(tf4) + 0.1f;
        rtc_talk_angleMultiply(&g->mV, tf2, cosine_weight);
        rtc_talk_angleMultiply(tf2, t2c, latitudeScale);
        s16 *lp = rtc_talk_angleFromDegrees(lat15, defaultLatitude);
        s16 va_storage, *va = &va_storage;
        rtc_talk_angleAdd(t2c, va, lp);
        s16 v = *va;
        if (v > (s16)*latHi) {
          v = *latHi;
          *va = v;
        }
        if (v < (s16)*latLo) {
          v = *latLo;
          *va = v;
        }
        work->mDirection.mV = rtc_talkAngleShortValue(tmp, v);
        rtc_angle_default(step);
        rtc_talk_angleSubtract(&g->mU, tf4, &work->mDirection.mU);
        s16 ts_storage, *ts = &ts_storage;
        if ((s16)*tf4 > zero)
          *step = (s16)*rtc_talk_angleFromDegrees(ts, 10.0f);
        else
          *step = (s16)*rtc_talk_angleFromDegrees(ts, -10.0f);
        bool done = false;
        for (s32 i = 36; i != 0; i--) {
          rtc_talk_angleSubtract(&work->mDirection.mU, t14, &g->mU);
          f64 dg = rtc_talk_angleDegree(t14); /* Degree */
          if (__builtin_fabs(dg) < 10.0f) {
            s16 t16_storage, *t16 = &t16_storage;
            rtc_talk_angleAdd(&work->mDirection.mU, t16, step);
            work->mDirection.mU = rtc_talkAngleShortValue(tmp, *t16);
            continue;
          }
          {
            rtc_vec x_storage, *x = &x_storage;
            rtc_vec e_storage, *e = &e_storage;
            rtc_vec pp_storage, *pp = &pp_storage;
            rtc_talk_globeXyz(&work->mDirection, x);
            rtc_vec_pl(&work->mCenter, e, x);
            rtc_copy(&work->mEye, e);
            rtc_positionOf(c, pp, ((rtc_actor *)(u32)c->mpPlayerActor));
            if (work->mEye.y < pp->y + c->m368) {
              rtc_vec pp2_storage, *pp2 = &pp2_storage;
              rtc_vec dd_storage, *dd = &dd_storage;
              rtc_positionOf(c, pp2, ((rtc_actor *)(u32)c->mpPlayerActor));
              work->mEye.y = (pp2->y + c->m368) + 10.0f;
              rtc_vec_mi(&work->mEye, dd, &work->mCenter);
              rtc_cSGlobe_Val(&work->mDirection, dd);
            }
          }
          bool hit = rtc_talk_lineBGCheck(c, at1, &work->mEye,
                                          0x8F) || /* lineBGCheck */
                     rtc_talk_lineBGCheck(c, at2, &work->mEye, 0x8F) ||
                     rtc_talk_lineBGCheck(c, &work->mCenter, &work->mEye, 0x8F);
          if (!hit) {
            rtc_vec q1_storage, *q1 = &q1_storage;
            rtc_vec q2_storage, *q2 = &q2_storage;
            rtc_cpXyz(q1, at1);
            rtc_cpXyz(q2, &work->mEye);
            hit = rtc_chkCamera(q1, q2, 15.0f, a1, a2);
            if (!hit) {
              rtc_vec q3_storage, *q3 = &q3_storage;
              rtc_vec q4_storage, *q4 = &q4_storage;
              rtc_cpXyz(q3, at2);
              rtc_cpXyz(q4, &work->mEye);
              if (!rtc_chkCamera(q3, q4, 15.0f, a1, a2)) {
                done = true;
                break;
              }
            }
          }
          /* blocked: rotate on */
          s16 t8_storage, *t8 = &t8_storage;
          s16 t12_storage, *t12 = &t12_storage;
          s16 t10_storage, *t10 = &t10_storage;
          s16 te_storage, *te = &te_storage;
          s16 l90_storage, *l90 = &l90_storage;
          rtc_talk_angleAdd(&work->mDirection.mU, t8, step);
          work->mDirection.mU = rtc_talkAngleShortValue(tmp, *t8);
          rtc_talk_angleSubtract(&g->mU, t12, &work->mDirection.mU);
          f32 c2 = rtc_talk_angleCos(t12) + 0.1f;
          rtc_talk_angleMultiply(&g->mV, t10, c2);
          rtc_talk_angleMultiply(t10, te, latitudeScale);
          s16 *l2 = rtc_talk_angleFromDegrees(l90, defaultLatitude);
          rtc_talk_angleAdd(te, t8, l2);
          s16 v2 = *t8;
          if (v2 > (s16)*latHi)
            v2 = *latHi;
          if (v2 < (s16)*latLo)
            v2 = *latLo;
          *va = v2;
          work->mDirection.mV = rtc_talkAngleShortValue(tmp, v2);
        }
        (void)done;
        rtc_talkFinish(c, work, a1, a2);
      }
    }
    if (work->mSide == -1) {
      s16 tfa_storage, *tfa = &tfa_storage;
      rtc_talk_angleSubtract(&g->mU, tfa, &work->mDirection.mU);
      work->mSide =
          ((s16)*tfa > rtc_talkAngleZero) ? 0 : 1; /* mfcr: !CR0[GT] */
    }
  }
  s32 type = work->mShotType;
  switch ((u32)type) {
  case RTC_TALK_PAIR: {
    if (a2 != 0 && rtc_fpcM_GetName(a2) == RTC_fpcNm_SHIP_e) {
      rtc_vec ofs_storage, *ofs = &ofs_storage;
      rtc_vec r_storage, *r = &r_storage;
      ofs->y = centerOffsetY;
      ofs->z = centerOffsetZ;
      ofs->x = centerOffsetX;
      rtc_talk_relationalPos(c, r, a1, a2, ofs, 0.25f);
      rtc_copy(&work->mCenter, r);
    }
    if (c->m100 != 0) {
      rtc_copy(&c->mViewCache.mCenter, &work->mCenter);
      rtc_copyTalkFloatBits(&c->mViewCache.mDirection.mRadius,
                            &work->mDirection.mRadius);
      rtc_copyTalkFloatBits(&c->mViewCache.mDirection.mV, &work->mDirection.mV);
      rtc_talkEye(c);
      rtc_copyTalkFloatBits(&c->mViewCache.mFovy, &work->mFovy);
    } else if (work->mSmoothless != 0) {
      rtc_flagsDone(c);
    } else {
      rtc_vec d_storage, *d = &d_storage;
      rtc_vec s_storage, *s = &s_storage;
      s16 tb0_storage, *tb0 = &tb0_storage;
      s16 tae_storage, *tae = &tae_storage;
      s16 tac_storage, *tac = &tac_storage;
      f32 r = (f32)(s32)((s32)((unsigned int)work->mTransitionFrames -
                               (unsigned int)work->mShotFrames));
      f32 k = r / work->mWeightSum;
      work->mFrameWeight = r;
      rtc_vec_mi(&work->mCenter, d, &c->mViewCache.mCenter);
      rtc_vec_ml(d, s, k);
      rtc_PSVECAdd(&c->mViewCache.mCenter, s, &c->mViewCache.mCenter);
      f32 rr = c->mViewCache.mDirection.mRadius;
      c->mViewCache.mDirection.mRadius =
          rtc_madd(work->mDirection.mRadius - rr, k, rr);
      rtc_talk_angleSubtract(&work->mDirection.mV, tb0,
                             &c->mViewCache.mDirection.mV);
      rtc_talk_angleMultiply(tb0, tae, k);
      rtc_talk_angleAdd(&c->mViewCache.mDirection.mV, tac, tae);
      c->mViewCache.mDirection.mV = rtc_talkAngleShortValue(tmp, *tac);
      rtc_talk_angleSubtract(&work->mDirection.mU, tac,
                             &c->mViewCache.mDirection.mU);
      rtc_talk_angleMultiply(tac, tae, k);
      rtc_talk_angleAdd(&c->mViewCache.mDirection.mU, tb0, tae);
      c->mViewCache.mDirection.mU = rtc_talkAngleShortValue(tmp, *tb0);
      rtc_talkEye(c);
      f32 fv = c->mViewCache.mFovy;
      c->mViewCache.mFovy = rtc_madd(work->mFovy - fv, k, fv);
      work->mWeightSum = work->mWeightSum - work->mFrameWeight;
      if ((s32)work->mShotFrames >=
          (s32)((unsigned int)work->mTransitionFrames - 1u))
        rtc_flagsDone(c);
      ret = false;
    }
    break;
  }
  case RTC_TALK_SHOULDER_A:
  case RTC_TALK_SHOULDER_B:
    if (type == RTC_TALK_SHOULDER_B)
      rtc_talkShoulder(c, work, a1, a2, 135.0f, 25.0f, 60.0f, false);
    else
      rtc_talkShoulder(c, work, a2, a1, 135.0f, 25.0f, 60.0f, false);
    break;
  case RTC_TALK_SHOULDER_NEAR_A:
  case RTC_TALK_SHOULDER_NEAR_B:
    if (type == RTC_TALK_SHOULDER_NEAR_B)
      rtc_talkShoulder(c, work, a1, a2, 93.0f, 5.0f, 50.0f, false);
    else
      rtc_talkShoulder(c, work, a2, a1, 93.0f, 5.0f, 50.0f, false);
    break;
  case RTC_TALK_SHOULDER_LOW_A:
  case RTC_TALK_SHOULDER_LOW_B:
    if (type == RTC_TALK_SHOULDER_LOW_B)
      rtc_talkShoulder(c, work, a1, a2, 135.0f, 15.0f, 45.0f, false);
    else
      rtc_talkShoulder(c, work, a2, a1, 135.0f, 15.0f, 45.0f, false);
    break;
  case RTC_TALK_SHOULDER_ATTN:
    rtc_talkShoulder(c, work, a1, a2, 200.0f, 68.0f, 55.0f, true);
    break;
  case RTC_TALK_EYE_A:
  case RTC_TALK_EYE_B: {
    rtc_actor *listener = (type == RTC_TALK_EYE_A) ? a1 : a2;
    rtc_actor *speaker = (type == RTC_TALK_EYE_A) ? a2 : a1;
    listener->actor_status = listener->actor_status | RTC_fopAcStts_NODRAW_e;
    rtc_vec pos_storage, *pos = &pos_storage;
    rtc_positionOf(c, pos, speaker);
    rtc_copy(&c->mViewCache.mCenter, pos);
    if (work->mShotFrames == 0) {
      rtc_vec e1_storage, *e1 = &e1_storage;
      rtc_vec e2_storage, *e2 = &e2_storage;
      rtc_vec d_storage, *d = &d_storage;
      rtc_eyePos(c, e1, listener);
      rtc_eyePos(c, e2, speaker);
      rtc_vec_mi(e1, d, e2);
      rtc_cSGlobe_Val(&c->mViewCache.mDirection, d);
      f32 R = c->mViewCache.mDirection.mRadius;
      rtc_flagsDone(c);
      c->mViewCache.mDirection.mRadius = R - 5.0f;
    }
    rtc_vec ep_storage, *ep = &ep_storage;
    rtc_eyePos(c, ep, speaker);
    rtc_copyTalkFloatBits(&c->mViewCache.mCenter.y, &ep->y);
    rtc_talkEye(c);
    c->mViewCache.mFovy = 50.0f;
    break;
  }
  case RTC_TALK_SIDE_A:
  case RTC_TALK_SIDE_B: {
    bool side =
        (type == RTC_TALK_SIDE_A) ? (work->mSide != 0) : (work->mSide == 0);
    rtc_actor *A = (type == RTC_TALK_SIDE_A) ? a2 : a1;
    rtc_actor *B = (type == RTC_TALK_SIDE_A) ? a1 : a2;
    rtc_talkSide(c, work, A, B, side, -10.0f, false, 75.0f, -80.0f, 135.0f,
                 0.0f, 50.0f);
    break;
  }
  case RTC_TALK_SIDE_HIGH_A:
  case RTC_TALK_SIDE_HIGH_B: {
    bool side = (type == RTC_TALK_SIDE_HIGH_A) ? (work->mSide != 0)
                                               : (work->mSide == 0);
    rtc_actor *A = (type == RTC_TALK_SIDE_HIGH_A) ? a2 : a1;
    rtc_actor *B = (type == RTC_TALK_SIDE_HIGH_A) ? a1 : a2;
    rtc_talkSide(c, work, A, B, side, -20.0f, false, 45.0f, -45.0f, 120.0f,
                 25.0f, 45.0f);
    break;
  }
  case RTC_TALK_SIDE_ATTN_A:
  case RTC_TALK_SIDE_ATTN_B: {
    bool side = (type == RTC_TALK_SIDE_ATTN_A) ? (work->mSide != 0)
                                               : (work->mSide == 0);
    rtc_actor *A = (type == RTC_TALK_SIDE_ATTN_A) ? a2 : a1;
    rtc_actor *B = (type == RTC_TALK_SIDE_ATTN_A) ? a1 : a2;
    rtc_talkSide(c, work, A, B, side, -20.0f, true, 35.0f, -30.0f, 145.0f,
                 -35.0f, 50.0f);
    break;
  }
  case RTC_TALK_CLOSEUP_A:
  case RTC_TALK_CLOSEUP_B:
  case RTC_TALK_CLOSEUP_WIDE_A:
  case RTC_TALK_CLOSEUP_WIDE_B: {
    rtc_actor *X =
        (type == RTC_TALK_CLOSEUP_B || type == RTC_TALK_CLOSEUP_WIDE_B) ? a2
                                                                        : a1;
    rtc_actor *Y =
        (type == RTC_TALK_CLOSEUP_B || type == RTC_TALK_CLOSEUP_WIDE_B) ? a1
                                                                        : a2;
    bool side = (type == RTC_TALK_CLOSEUP_B || type == RTC_TALK_CLOSEUP_WIDE_B)
                    ? (work->mSide != 0)
                    : (work->mSide == 0);
    if (work->mShotFrames != 0)
      break;
    rtc_vec e1_storage, *e1 = &e1_storage;
    rtc_vec pX_storage, *pX = &pX_storage;
    rtc_vec eY_storage, *eY = &eY_storage;
    rtc_vec o_storage, *o = &o_storage;
    rtc_vec oB_storage, *oB = &oB_storage;
    rtc_vec a_storage, *a = &a_storage;
    rtc_vec b_storage, *b = &b_storage;
    rtc_vec d_storage, *d = &d_storage;
    rtc_vec x_storage, *x = &x_storage;
    rtc_vec s_storage, *s = &s_storage;
    rtc_globe g164_storage, *g164 = &g164_storage;
    rtc_globe g30_storage, *g30 = &g30_storage;
    s16 t_storage, *t = &t_storage;
    s16 tmp2_storage, *tmp2 = &tmp2_storage;
    rtc_eyePos(c, e1, X);
    rtc_positionOf(c, pX, X);
    f32 h = (e1->y - pX->y) * 1.2f;
    rtc_eyePos(c, eY, Y);
    rtc_eyePos(c, e1, X);
    o->z = work->mActorDirection.mRadius * 0.25f;
    o->y = 0.0f;
    f32 dy = (eY->y - e1->y) * 0.25f;
    oB->y = dy;
    f32 cy = rtc_madd(h, 0.5f, dy);
    oB->z = -75.0f;
    if (!side) {
      oB->x = 75.0f;
      o->x = 25.0f;
    } else {
      o->x = -25.0f;
      oB->x = -75.0f;
    }
    rtc_attentionPos(c, a, Y);
    rtc_attentionPos(c, b, X);
    rtc_vec_mi(a, d, b);
    rtc_talk_globeFromVector(g164, d);
    rtc_talk_globeFromVector(g30, o);
    rtc_talk_angleAdd(&g30->mU, t, &g164->mU);
    g30->mU = rtc_talkAngleShortValue(tmp2, *t);
    rtc_attentionPos(c, d, X);
    rtc_talk_globeXyz(g30, x);
    rtc_vec_pl(d, s, x);
    rtc_copy(&c->mViewCache.mCenter, s);
    rtc_positionOf(c, s, X);
    c->mViewCache.mCenter.y = s->y + cy;
    rtc_cSGlobe_Val(g30, oB);
    s16 inv = rtc_cSAngle_Inv(&g164->mU);
    rtc_talk_angleAddShort(&g30->mU, t, inv); /* + s16 */
    g30->mU = rtc_talkAngleShortValue(tmp2, *t);
    rtc_attentionPos(c, d, Y);
    rtc_talk_globeXyz(g30, x);
    rtc_vec_pl(d, s, x);
    rtc_copy(&c->mViewCache.mEye, s);
    rtc_vec_mi(&c->mViewCache.mEye, d, &c->mViewCache.mCenter);
    rtc_cSGlobe_Val(&c->mViewCache.mDirection, d);
    rtc_flagsDone(c);
    if (work->mShotType == RTC_TALK_CLOSEUP_A ||
        work->mShotType == RTC_TALK_CLOSEUP_B)
      c->mViewCache.mFovy = 55.0f;
    else
      c->mViewCache.mFovy = 65.0f;
    break;
  }
  case RTC_TALK_WIDE: {
    if (work->mShotFrames == 0)
      rtc_flagsDone(c);
    rtc_copy(&c->mViewCache.mCenter, &work->mCenter);
    s16 l_storage, *l = &l_storage;
    s16 step_storage, *step = &step_storage;
    s16 t18_storage, *t18 = &t18_storage;
    s16 *lp = rtc_talk_angleFromDegrees(l, 35.0f);
    rtc_talk_globeVal(&c->mViewCache.mDirection, 350.0f, lp,
                      &work->mDirection.mU);
    s16 v = c->mViewCache.mDirection.mV;
    if (v > (s16)*latHi) {
      v = rtc_talkAngleShortValue(tmp, *latHi);
      c->mViewCache.mDirection.mV = v;
    }
    if (v < (s16)*latLo)
      c->mViewCache.mDirection.mV = rtc_talkAngleShortValue(tmp, *latLo);
    rtc_vec at1_storage, *at1 = &at1_storage;
    rtc_vec at2_storage, *at2 = &at2_storage;
    rtc_attentionPos(c, at1, a1);
    rtc_attentionPos(c, at2, a2);
    rtc_angle_default(step);
    s16 ts_storage, *ts = &ts_storage;
    if (work->mSide != 0)
      *step = (s16)*rtc_talk_angleFromDegrees(ts, 20.0f);
    else
      *step = (s16)*rtc_talk_angleFromDegrees(ts, -20.0f);
    for (s32 i = 18; i != 0; i--) {
      rtc_talkEye(c);
      bool hit = rtc_talk_lineBGCheck(c, at1, &c->mViewCache.mEye, 0x8F) ||
                 rtc_talk_lineBGCheck(c, at2, &c->mViewCache.mEye, 0x8F);
      if (!hit) {
        rtc_vec q1_storage, *q1 = &q1_storage;
        rtc_vec q2_storage, *q2 = &q2_storage;
        rtc_cpXyz(q1, at1);
        rtc_cpXyz(q2, &c->mViewCache.mEye);
        hit = rtc_chkCamera(q1, q2, 15.0f, a1, a2);
        if (!hit) {
          rtc_vec q3_storage, *q3 = &q3_storage;
          rtc_vec q4_storage, *q4 = &q4_storage;
          rtc_cpXyz(q3, at2);
          rtc_cpXyz(q4, &c->mViewCache.mEye);
          if (!rtc_chkCamera(q3, q4, 15.0f, a1, a2))
            break;
        }
      }
      rtc_talk_angleAdd(&c->mViewCache.mDirection.mU, t18, step);
      c->mViewCache.mDirection.mU = rtc_talkAngleShortValue(tmp, *t18);
    }
    c->mViewCache.mFovy = 60.0f;
    break;
  }
  case RTC_TALK_MIDPOINT: {
    if (work->mShotFrames == 0)
      rtc_flagsDone(c);
    rtc_vec b_storage, *b = &b_storage;
    rtc_vec a_storage, *a = &a_storage;
    rtc_vec d_storage, *d = &d_storage;
    rtc_vec h_storage, *h = &h_storage;
    rtc_vec center_storage, *center = &center_storage;
    rtc_globe g_storage, *g = &g_storage;
    s16 side_storage, *side = &side_storage;
    s16 u_storage, *u = &u_storage;
    rtc_attentionPos(c, b, a2);
    rtc_attentionPos(c, a, a1);
    rtc_vec_mi(b, d, a);
    rtc_talk_globeFromVector(g, d);
    rtc_angle_default(side);
    if (work->mSide != 0)
      *side = rtc_talkAngle270; /* _270 */
    else
      *side = rtc_talkAngle90; /* _90 */
    rtc_attentionPos(c, b, a1);
    rtc_vec_ml(d, h, 0.5f);
    rtc_vec_pl(b, center, h);
    rtc_copyTalkFloatBits(&c->mViewCache.mCenter.x, &center->x);
    rtc_copyTalkFloatBits(&c->mViewCache.mCenter.z, &center->z);
    c->mViewCache.mCenter.y = center->y - 30.0f;
    rtc_talk_angleAdd(&g->mU, u, side);
    rtc_talk_globeVal(&c->mViewCache.mDirection, 200.0f,
                      (s16 *)(u32)&rtc_talkAngleZero, u);
    rtc_talkEye(c);
    c->mViewCache.mFovy = 60.0f;
    f64 r = rtc_radiusActorInSight(c, a1, a2);
    if (r > 0.0f) {
      c->mViewCache.mDirection.mRadius =
          (f32)((f64)c->mViewCache.mDirection.mRadius + r);
      rtc_talkEye(c);
    }
    break;
  }
  default:
    break;
  }
  work->mPreviousShotType = work->mShotType;
  work->mShotFrames = (s32)((unsigned int)work->mShotFrames + 1u);
  return ret;
}
