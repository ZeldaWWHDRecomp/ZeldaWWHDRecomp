/* Complete public CC0 daArrow_c::procMove02054040, d_a_arrow.cpp a874e1a9.
 * MIT fix_vanilla_bugs: clear atHit_CB only at the joint-stop transition.
 * All other moving/reflected/background/water paths and native helper calls
 * stay. */
#pragma once
#ifndef R_ARROW
#define R_ARROW(t, a) (*(volatile t *)(a))
#endif
WWHD_GAME_FUNC(0x028E8D88, void, ram_add, (u32 a, u32 b, u32 out));
WWHD_GAME_FUNC(0x0201AD78, void, ram_add_value, (u32 a, u32 out, u32 b));
WWHD_GAME_FUNC(0x0201ADE0, void, ram_subtract, (u32 a, u32 out, u32 b));
WWHD_GAME_FUNC(0x0201AE48, void, ram_multiply, (u32 a, u32 out, f32 value));
WWHD_GAME_FUNC(0x028E8E64, void, ram_scale, (u32 a, u32 out, f32 value));
WWHD_GAME_FUNC(0x028E8DAC, void, ram_subtract_into, (u32 a, u32 b, u32 out));
WWHD_GAME_FUNC(0x028E8DD0, f64, ram_square, (u32 vector));
WWHD_GAME_FUNC(0x028F4384, f64, ram_sqrt, (f64 square));
WWHD_GAME_FUNC(0x020195B0, s32, ram_atan, (f64 y, f64 x));
WWHD_GAME_FUNC(0x024F1AFC, void, ram_line_set,
               (u32 check, u32 start, u32 end, u32 actor));
WWHD_GAME_FUNC(0x028E90D4, void, ram_matrix_copy, (u32 from, u32 to));
WWHD_GAME_FUNC(0x025160DC, s32, ram_attack_hit, (u32 collision));
WWHD_GAME_FUNC(0x02515BBC, u32, ram_hit_actor, (u32 attack));
WWHD_GAME_FUNC(0x02053B18, void, ram_reflect, (u32 actor));
WWHD_GAME_FUNC(0x025538E4, s32, ram_joint_hit,
               (u32 joint, u32 hit, u32 angle, u32 pos, u32 rotation));
WWHD_GAME_FUNC(0x025A5AC8, void, ram_follow_end, (u32 follow));
WWHD_GAME_FUNC(0x02053DDC, void, ram_stop_matrix, (u32 actor));
WWHD_GAME_FUNC(0x025200D4, u32, ram_game, (void));
WWHD_GAME_FUNC(0x02008860, s32, ram_line_cross, (u32 background, u32 check));
WWHD_GAME_FUNC(0x020532B8, s32, ram_water, (u32 actor));
WWHD_GAME_FUNC(0x0255F3E8, void, ram_color, (u32 position, s32 color));
WWHD_GAME_FUNC(0x020084C8, u32, ram_triangle,
               (u32 background, u32 bg, u32 polygon));
WWHD_GAME_FUNC(0x028E93CC, void, ram_translate,
               (u32 matrix, f32 x, f32 y, f32 z));
WWHD_GAME_FUNC(0x025F1B48, void, ram_rotate, (u32 matrix, s32 x, s32 y, s32 z));
WWHD_GAME_FUNC(0x025A847C, u32, ram_particle,
               (u32 control, s32 group, u32 id, u32 position, u32 angle,
                u32 scale, s32 alpha, u32 callback, s32 setup, u32 color,
                u32 env, u32 scale2));
WWHD_GAME_FUNC(0x02520540, s32, ram_reverb, (s32 room));
WWHD_GAME_FUNC(0x025E1A40, void, ram_sound,
               (u32 id, u32 position, u32 parameter, s32 reverb));
WWHD_GAME_FUNC(0x025D57E0, void, ram_delete, (u32 actor));
WWHD_GAME_FUNC(0x024EEE30, s32, ram_group,
               (u32 background, u32 polygon, u32 mask));
WWHD_GAME_FUNC(0x025D5834, u32, ram_create,
               (s32 name, u32 params, u32 pos, s32 room, u32 angle, u32 scale,
                s32 subtype, u32 callback));
WWHD_GAME_FUNC(0x025D5A20, u32, ram_create_child,
               (s32 name, u32 parent, u32 params, u32 pos, s32 room, u32 angle,
                u32 scale, s32 subtype, u32 callback));
WWHD_GAME_FUNC(0x024EF0F4, u32, ram_attribute, (u32 background, u32 polygon));
WWHD_GAME_FUNC(0x024EECAC, u32, ram_material, (u32 background, u32 polygon));
WWHD_GAME_FUNC(0x02017300, void, ram_vector_angle, (u32 normal, u32 angle));
WWHD_GAME_FUNC(0x02053700, void, ram_blur, (u32 actor));
WWHD_GAME_FUNC(0x020181B0, void, ram_capsule,
               (u32 capsule, u32 start, u32 end, f32 radius));
WWHD_GAME_FUNC(0x0200E240, void, ram_collision_set,
               (u32 manager, u32 collision));
WWHD_GAME_FUNC(0x02516C14, void, ram_mass_set,
               (u32 manager, u32 collision, u32 priority));
static void ram_copy(u32 to, u32 from) {
  /* Character accesses may initialize typed float temporaries without alias UB. */
  for (u32 i = 0; i < 12; i++)
    R_ARROW(u8, to + i) = R_ARROW(u8, from + i);
}
static u32 ram_stack(void) { return (u32)&WWHD_GAME_DATA(0x1048D0CC, u8); }
static void ram_proc(u32 actor, u32 function) {
  R_ARROW(s16, actor + 0x7A8) = 0;
  R_ARROW(s16, actor + 0x7AA) = -1;
  R_ARROW(u32, actor + 0x7AC) = function;
}
static s32 ram_is_water(u32 actor) {
  return R_ARROW(s16, actor + 0x7AA) == -1 &&
         R_ARROW(s16, actor + 0x7A8) == 0 &&
         R_ARROW(u32, actor + 0x7AC) == 0x02055734;
}
static f64 ram_length(u32 vector) { return ram_sqrt(ram_square(vector)); }
static f64 ram_xz(f32 x, f32 z) {
  f32 v[3] = {x, 0.0f, z};
  return ram_length((u32)v);
}
static void ram_audio(u32 actor, u32 id, u32 parameter) {
  const s32 reverb = ram_reverb(R_ARROW(s8, actor + 0x326));
  ram_sound(id, actor + 0x314, parameter, reverb);
}
static void ram_effect(u32 id, u32 pos, u32 angle) {
  const u32 control = R_ARROW(u32, ram_game() + 0x5AB0);
  (void)ram_particle(control, 1, id, pos, angle, 0, 255, 0, -1, 0, 0, 0);
}
static void ram_model(u32 actor) {
  ram_translate(ram_stack(), R_ARROW(f32, actor + 0x314),
                R_ARROW(f32, actor + 0x318), R_ARROW(f32, actor + 0x31C));
  ram_rotate(ram_stack(), R_ARROW(s16, actor + 0x328),
             R_ARROW(s16, actor + 0x32A), 0);
  /* The public inline model setter snapshots all12 words before any stores. */
  unsigned int matrix[12];
  for (u32 i = 0; i < 12; i++)
    matrix[i] = (unsigned int)R_ARROW(u32, ram_stack() + 4 * i);
  const u32 model = R_ARROW(u32, actor + 0x3B0);
  for (u32 i = 0; i < 12; i++)
    R_ARROW(u32, model + 0xC8 + 4 * i) = matrix[i];
}
static void ram_rebound(u32 actor) {
  ram_proc(actor, 0x02055450);
  R_ARROW(u32, actor + 0xB0) = 3;
  ram_scale(actor + 0x33C, actor + 0x33C, -0.1f);
  const f64 xz =
      ram_xz(R_ARROW(f32, actor + 0x33C), R_ARROW(f32, actor + 0x344));
  const f32 y = (f32)((f64)R_ARROW(f32, actor + 0x340) + xz);
  ram_copy(actor + 0x314, actor + 0x300);
  R_ARROW(s16, actor + 0x7B4) = 0x2C00;
  R_ARROW(f32, actor + 0x340) = y;
}
WWHD_REPLACE(0x02054040, s32, randomizer_arrow_move, (u32 actor)) {
  R_ARROW(f32, actor + 0x370) = 100.0f;
  ram_add(actor + 0x314, actor + 0x33C, actor + 0x314);
  f32 step[3], sum[3], quarter[3], hit[3];
  ram_multiply(actor + 0x33C, (u32)step, 0.25f);
  ram_add_value(actor + 0x314, (u32)sum, (u32)step);
  ram_copy((u32)quarter, (u32)sum);
  ram_line_set(actor + 0x3BC, actor + 0x300, (u32)quarter, actor);
  const u16 sy = R_ARROW(u16, actor + 0x32A), sx = R_ARROW(u16, actor + 0x328),
            sz = R_ARROW(u16, actor + 0x32C);
  R_ARROW(u16, actor + 0x802) = sz;
  R_ARROW(u16, actor + 0x7FE) = sx;
  R_ARROW(u16, actor + 0x800) = sy;
  const u32 model = R_ARROW(u32, actor + 0x3B0);
  ram_matrix_copy(model ? model + 0xC8 : 0, actor + 0x7CC);
  R_ARROW(s16, actor + 0x79C) = (s16)(R_ARROW(s16, actor + 0x79C) + 0x889);
  s32 hit_type = 0, bg_check = 1;
  if (ram_attack_hit(actor + 0x4B8)) {
    u32 reflected = 0;
    if (R_ARROW(u8, actor + 0x71D) == 3 && !R_ARROW(u8, actor + 0x780) &&
        (R_ARROW(u32, actor + 0x50C) & 1)) {
      const u32 target = ram_hit_actor(actor + 0x508);
      if (target && R_ARROW(s16, target + 8) == 0xA8)
        reflected = 1;
    }
    if (reflected) {
      (void)ram_hit_actor(actor + 0x508);
      ram_copy((u32)hit, actor + 0x528);
      R_ARROW(u8, actor + 0x780) = 1;
      ram_reflect(actor);
    } else {
      u32 target;
      u32 blocked;
      if (R_ARROW(u8, actor + 0x818)) {
        ram_copy((u32)hit, actor + 0x808);
        target = R_ARROW(u32, actor + 0x804);
        blocked = 0;
      } else {
        target = ram_hit_actor(actor + 0x508);
        ram_copy((u32)hit, actor + 0x528);
        blocked = R_ARROW(u32, actor + 0x50C) & 1;
      }
      if (target) {
        const u32 joint = R_ARROW(u32, target + 0x36C);
        if (R_ARROW(u8, actor + 0x71D) == 3) {
          s32 ganon = 0;
          const s16 names[3] = {0xF3, 0xF4, 0xF5};
          for (u32 i = 0; i < 3; i++) {
            const u32 candidate = ram_hit_actor(actor + 0x508);
            if (candidate && R_ARROW(s16, candidate + 8) == names[i]) {
              ganon = 1;
              break;
            }
          }
          if (ganon && blocked) {
            ram_copy(actor + 0x7C0, (u32)hit);
            f32 back[3], pos[3];
            ram_multiply(actor + 0x33C, (u32)back, 0.25f);
            ram_subtract((u32)hit, (u32)pos, (u32)back);
            ram_copy(actor + 0x314, (u32)pos);
            if (!R_ARROW(u8, actor + 0x7FC))
              ram_color(actor + 0x314, 2);
            const f32 vx = R_ARROW(f32, actor + 0x33C),
                      vz = R_ARROW(f32, actor + 0x344);
            R_ARROW(u32, actor + 0x2E0) |= 0x4000;
            ram_proc(actor, 0x02054FE0);
            R_ARROW(u32, actor + 0xB0) = 2;
            R_ARROW(s16, actor + 0x720) = 40;
            s16 angle[3];
            const f64 xz = ram_xz(vx, vz);
            angle[0] = (s16)ram_atan(R_ARROW(f32, actor + 0x340), xz);
            angle[1] = (s16)ram_atan(R_ARROW(f32, actor + 0x33C),
                                     R_ARROW(f32, actor + 0x344));
            angle[2] = 0;
            ram_translate(ram_stack(), R_ARROW(f32, actor + 0x314),
                          R_ARROW(f32, actor + 0x318),
                          R_ARROW(f32, actor + 0x31C));
            ram_rotate(ram_stack(), angle[0], angle[1], 0);
            ram_matrix_copy(ram_stack(), actor + 0x7CC);
            ram_effect(0x2A1, actor + 0x7C0, (u32)angle);
            ram_audio(actor, 0x69DC, 0);
            ram_delete(actor);
          }
        } else if (blocked) {
          hit_type = 1;
          R_ARROW(s16, actor + 0x720) = 40;
          R_ARROW(u32, actor + 0x2E0) |= 0x4000;
        } else if (joint) {
          f32 pos[3];
          s16 angle[3];
          const s32 index = ram_joint_hit(joint, (u32)hit, actor + 0x328,
                                          (u32)pos, (u32)angle);
          R_ARROW(s32, actor + 0x730) = index;
          if (index >= 0) {
            const u16 rx = (u16)angle[0], ry = (u16)angle[1];
            R_ARROW(u16, actor + 0x7FE) = rx;
            R_ARROW(u16, actor + 0x800) = ry;
            const unsigned int y = R_ARROW(u32, (u32)pos + 4),
                               z = R_ARROW(u32, (u32)pos + 8);
            R_ARROW(u32, actor + 0x738) = y;
            R_ARROW(u32, actor + 0x73C) = z;
            R_ARROW(s16, actor + 0x802) = angle[2];
            R_ARROW(u32, actor + 0x2E0) |= 0x4000;
            R_ARROW(s16, actor + 0x720) = 40;
            hit_type = 2;
            R_ARROW(u32, actor + 0x734) = R_ARROW(u32, (u32)pos);
          } else if (index == -3) {
            ram_delete(actor);
            return 1;
          }
        }
      }
    }
  }
  if (hit_type > 0) {
    bg_check = 0;
    if (R_ARROW(u32, actor + 0x788))
      ram_follow_end(actor + 0x784);
    if (hit_type == 1) {
      ram_rebound(actor);
      ram_model(actor);
      ram_audio(actor, 0x287A, 0x20);
    } else if (hit_type == 2) {
      /* Exact upstream seam02054658: delta was stored, callback is cleared
       * before the native stopped parameter/procedure writes and any effect
       * callback. */
      R_ARROW(s16, actor + 0x7A8) = 0;
      R_ARROW(u32, actor + 0x510) = 0;
      const u8 type = R_ARROW(u8, actor + 0x71D);
      R_ARROW(u32, actor + 0xB0) = 2;
      R_ARROW(s16, actor + 0x7AA) = -1;
      R_ARROW(u32, actor + 0x7AC) = 0x020553C0;
      if (type == 1) {
        ram_audio(actor, 0x69DA, 0);
        R_ARROW(u8, actor + 0x7B0) = 0;
      } else if (type == 2)
        ram_audio(actor, 0x69DB, 0);
      else if (type == 3) {
        ram_audio(actor, 0x69DC, 0);
        R_ARROW(u8, actor + 0x7B0) = 0;
      }
      ram_stop_matrix(actor);
    }
  }
  if (bg_check) {
    if (ram_line_cross(ram_game() + 0x12A0, actor + 0x3BC)) {
      const u32 cross = actor + 0x3EC;
      ram_copy(actor + 0x7C0, cross);
      f32 back[3], pos[3];
      ram_multiply(actor + 0x33C, (u32)back, 0.25f);
      ram_subtract(cross, (u32)pos, (u32)back);
      ram_copy(actor + 0x314, (u32)pos);
      if (!ram_water(actor)) {
        u8 type = R_ARROW(u8, actor + 0x71D);
        if (type >= 1 && type <= 3 && !R_ARROW(u8, actor + 0x7FC))
          ram_color(actor + 0x314,
                    R_ARROW(u8, (u32)&WWHD_GAME_DATA(0x10007477, u8) + type));
        ram_proc(actor, 0x02054FE0);
        R_ARROW(s16, actor + 0x720) = 40;
        R_ARROW(u32, actor + 0xB0) = 2;
        R_ARROW(u32, actor + 0x2E0) |= 0x4000;
        const u32 background = ram_game() + 0x12A0;
        const u32 triangle =
            ram_triangle(background, R_ARROW(u16, actor + 0x3BE),
                         R_ARROW(u16, actor + 0x3BC));
        s16 angle[3];
        if (triangle) {
          const f64 xz =
              ram_xz(R_ARROW(f32, triangle), R_ARROW(f32, triangle + 8));
          angle[0] = (s16)ram_atan(-R_ARROW(f32, triangle + 4), -xz);
          angle[1] = (s16)ram_atan(-R_ARROW(f32, triangle),
                                   -R_ARROW(f32, triangle + 8));
          angle[2] = 0;
        } else {
          angle[1] = 0;
          angle[2] = 0;
          angle[0] = 0;
        }
        ram_translate(ram_stack(), R_ARROW(f32, actor + 0x314),
                      R_ARROW(f32, actor + 0x318), R_ARROW(f32, actor + 0x31C));
        ram_rotate(ram_stack(), angle[0], angle[1], 0);
        ram_matrix_copy(ram_stack(), actor + 0x7CC);
        type = R_ARROW(u8, actor + 0x71D);
        if (type == 1) {
          ram_effect(0x29A, actor + 0x7C0, (u32)angle);
          ram_effect(0x29B, actor + 0x7C0, (u32)angle);
          ram_audio(actor, 0x69DA, 0);
          R_ARROW(u8, actor + 0x7B0) = 0;
        } else if (type == 2) {
          if (ram_group(ram_game() + 0x12A0, actor + 0x3BC, 0x200))
            ram_create(0x2A, 0, actor + 0x7C0, R_ARROW(s8, actor + 0x326), 0, 0,
                       -1, 0);
          else {
            ram_effect(0x29E, actor + 0x7C0, (u32)angle);
            ram_create_child(0x1D9, R_ARROW(u32, actor + 4),
                             R_ARROW(u8, actor + 0x71D), actor + 0x7C0,
                             R_ARROW(s8, actor + 0x326), actor + 0x7FE, 0, -1,
                             0);
            ram_audio(actor, 0x69DB, 0);
          }
        } else if (type == 3) {
          ram_effect(0x2A1, actor + 0x7C0, (u32)angle);
          ram_audio(actor, 0x69DC, 0);
          R_ARROW(u8, actor + 0x7B0) = 0;
        }
        const u32 attribute = ram_attribute(ram_game() + 0x12A0, actor + 0x3BC);
        const u32 material = ram_material(ram_game() + 0x12A0, actor + 0x3BC);
        if (R_ARROW(u8, actor + 0x71D) == 0 &&
            (attribute == 3 || attribute == 9 || attribute == 15 ||
             (attribute >= 20 && attribute <= 21))) {
          ram_rebound(actor);
          s16 normal_angle[3];
          ram_vector_angle(triangle, (u32)normal_angle);
          ram_effect(0xC, actor + 0x7C0, (u32)normal_angle);
          ram_audio(actor, 0x287A, material);
        } else
          ram_audio(actor, 0x2879, material);
      }
    } else
      (void)ram_water(actor);
  }
  if (ram_is_water(actor))
    ram_model(actor);
  else {
    f32 delta[3];
    ram_subtract(actor + 0x314, (u32)delta, actor + 0x7C0);
    if (ram_length((u32)delta) > 25000.0) {
      f32 y = R_ARROW(f32, actor + 0x340) - 2.0f;
      if (y < -100.0f)
        y = -100.0f;
      const f32 pos_y = R_ARROW(f32, actor + 0x318),
                start_y = R_ARROW(f32, actor + 0x7C4);
      R_ARROW(f32, actor + 0x340) = y;
      if (start_y > pos_y) {
        ram_delete(actor);
        return 1;
      }
    } else {
      f32 delta2[3];
      ram_subtract(actor + 0x314, (u32)delta2, actor + 0x7C0);
      if (ram_length((u32)delta2) > 20000.0)
        R_ARROW(u8, actor + 0x7FC) = 1;
    }
    ram_blur(actor);
    ram_model(actor);
    f32 step2[3], end[3];
    ram_multiply(actor + 0x33C, (u32)step2, 1.25f);
    ram_add_value(actor + 0x314, (u32)delta, (u32)step2);
    ram_copy((u32)end, (u32)delta);
    ram_capsule(actor + 0x5D0, actor + 0x314, (u32)end, 5.0f);
    ram_subtract_into(actor + 0x5DC, actor + 0x5D0, actor + 0x534);
    ram_collision_set(ram_game() + 0x26A4, actor + 0x4B8);
    ram_mass_set(ram_game() + 0x4EF8, actor + 0x4B8, 1);
  }
  return 1;
}
