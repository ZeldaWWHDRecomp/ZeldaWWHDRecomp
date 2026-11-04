/* CC0 full inlined damage_check from daMo2_Execute, source a874e1a9. */
#pragma once
WWHD_GAME_FUNC(0x02515E50,void,randomizer_mo2_status_move,(u32 status));
WWHD_GAME_FUNC(0x0200E240,void,randomizer_mo2_collision_add,(u32 manager,u32 object));
WWHD_GAME_FUNC(0x02018D40,void,randomizer_mo2_sphere_center,(u32 sphere,u32 position));
WWHD_GAME_FUNC(0x02018C8C,void,randomizer_mo2_sphere_radius,(u32 sphere,f32 radius));
WWHD_GAME_FUNC(0x025162A4,s32,randomizer_mo2_target_hit,(u32 object));
WWHD_GAME_FUNC(0x02516300,u32,randomizer_mo2_hit_object,(u32 object));
WWHD_GAME_FUNC(0x02518CC8,void,randomizer_mo2_defense_sound,(u32 actor,u32 object,u32 material));
WWHD_GAME_FUNC(0x025A847C,u32,randomizer_mo2_particle,(u32 control,u32 group,u32 id,u32 position,u32 angle,u32 scale,u32 alpha,u32 callback,s32 setup,u32 color,u32 environment,u32 scale2d));
WWHD_GAME_FUNC(0x02041C30,void,randomizer_mo2_fire_remove,(u32 fire));
WWHD_GAME_FUNC(0x021C9004,void,randomizer_mo2_path_check,(u32 actor));
WWHD_GAME_FUNC(0x021C8E30,void,randomizer_mo2_wait,(u32 actor));
WWHD_GAME_FUNC(0x02518DB0,void,randomizer_mo2_attack_power,(u32 info));
WWHD_GAME_FUNC(0x025192A8,u32,randomizer_mo2_attack_check,(u32 actor,u32 info));
WWHD_GAME_FUNC(0x0255F458,void,randomizer_mo2_sound_influence,(u32 position,s32 radius,u32 id,s32 type));
WWHD_GAME_FUNC(0x025E1AA4,void,randomizer_mo2_monster_sound,(u32 id,u32 position,u32 actor_id,u32 parameter,s32 reverb));
struct randomizer_mo2_attack_info {
    unsigned int object,actor;
    u8 damage,dead,type,padding;
    s16 angle[3];u16 cut;
    unsigned int particle_position;
    s32 hit_sound;
};
_Static_assert(sizeof(struct randomizer_mo2_attack_info)==0x1C,"Moblin attack ABI");
static void randomizer_mo2_voice(u32 actor,u32 id,u32 parameter) {
    if(actor+0x37C) {
        const s32 room=R_MO2(s8,actor+0x326);
        const u32 process=actor?R_MO2(u32,actor+4):0xFFFFFFFFu;
        randomizer_mo2_monster_sound(id,actor+0x37C,process,parameter,randomizer_mo2_reverb(room));
    }
}
static u8 randomizer_mo2_damage(u32 actor) {
    const u32 player=R_MO2(u32,randomizer_mo2_game()+0x5B2C);
    struct randomizer_mo2_attack_info info;
    info.object=0;info.damage=0;info.cut=0;
    u8 hit_part=0,reaction=0;
    randomizer_mo2_status_move(actor+0x1030);
    randomizer_mo2_collision_add(randomizer_mo2_game()+0x26A4,actor+0x2820);
    randomizer_mo2_sphere_center(actor+0x2938,actor+0x2D30);
    if(randomizer_mo2_target_hit(actor+0x2820)) {
        const u32 object=randomizer_mo2_hit_object(actor+0x2820);
        randomizer_mo2_defense_sound(actor,object,0x41);
        randomizer_mo2_rotate_y_set(randomizer_mo2_matrix(),R_MO2(s16,actor+0x32A));
        f32 vector[3]={0.0f,0.0f,-10.0f};
        randomizer_mo2_transform((u32)vector,actor+0xD1C);
        R_MO2(f32,actor+0xDC4)=-15.0f;
        s16 angle[3];
        angle[0]=R_MO2(s16,player+0x328);
        const s16 y=R_MO2(s16,player+0x32A);
        angle[1]=y;
        const s16 z=R_MO2(s16,player+0x32C);
        angle[1]=(s16)(y-0x8000);angle[2]=z;
        const u32 control=R_MO2(u32,randomizer_mo2_game()+0x5AB0);
        const u32 emitter=randomizer_mo2_particle(control,0,0x2B,actor+0x2D30,(u32)angle,0,255,0,-1,actor+0x1A8,actor+0x1A8,0);
        if(emitter) {
            R_MO2(s32,emitter+0x5C)=1;
            R_MO2(f32,emitter+0x34)=10.0f;
            R_MO2(f32,emitter+0x58)=0.2f;
            R_MO2(f32,emitter+0x7C)=0.15f;
            const f32 scale=WWHD_GAME_DATA(0x1047BE30,f32)+0.85f;
            R_MO2(f32,emitter+0x238)=scale;R_MO2(f32,emitter+0x23C)=scale;R_MO2(f32,emitter+0x240)=scale;
        }
        return 0;
    }
    R_MO2(f32,actor+0x2D34)=-10000.0f;
    randomizer_mo2_sphere_radius(actor+0x2938,-200.0f);
    if(R_MO2(s16,actor+0x858)==0 &&
       (randomizer_mo2_target_hit(actor+0x26F4)||randomizer_mo2_target_hit(actor+0x25C4))) {
        R_MO2(s32,actor+0x844)=0;R_MO2(s16,actor+0x2D4C)=-1;
        R_MO2(s16,actor+0x2D56)=0;R_MO2(s16,actor+0x858)=4;
        u32 object;
        if(randomizer_mo2_target_hit(actor+0x26F4)) {
            hit_part=1;object=randomizer_mo2_hit_object(actor+0x26F4);
            info.object=(unsigned int)object;info.particle_position=(unsigned int)(actor+0x26F4+0xCC);
        } else if(randomizer_mo2_target_hit(actor+0x25C4)) {
            hit_part=2;object=randomizer_mo2_hit_object(actor+0x25C4);
            info.object=(unsigned int)object;info.particle_position=(unsigned int)(actor+0x25C4+0xCC);
        } else object=info.object;
        if(object) {
            if(R_MO2(u32,object+0x10)&0x180000) {
                if(R_MO2(u32,object+0x10)&0x80000) {
                    R_MO2(s16,actor+0x8C2)=0;
                    R_MO2(s16,actor+0x2E58)=(s16)(WWHD_GAME_DATA(0x1047B68E,s16)+300);
                    randomizer_mo2_path_check(actor);randomizer_mo2_wait(actor);
                    R_MO2(s16,actor+0x8C0)=-10;
                } else R_MO2(s8,actor+0x2E5A)=1;
                randomizer_mo2_fire_remove(actor+0x320C);
                R_MO2(s16,actor+0x898)=0;
                randomizer_mo2_smoke_end(actor+0x89C);
                if(R_MO2(s8,actor+0x10B0)!=0)R_MO2(s8,actor+0x10B1)=2;
                R_MO2(u8,actor+0x2E0D)=1;
                return 0;
            }
            if(R_MO2(u32,object+0x10)&0x40200) {
                R_MO2(s16,actor+0x858)=50;
                R_MO2(s16,actor+0x3210)=(s16)(WWHD_GAME_DATA(0x1047B68C,s16)+100);
            }
        }
        const s8 health=R_MO2(s8,actor+0x3A1);
        randomizer_mo2_attack_power((u32)&info);
        if(info.type==10||info.type==14)R_MO2(s8,actor+0x3A1)=20;
        info.actor=(unsigned int)randomizer_mo2_attack_check(actor,(u32)&info);
        if(info.type==10||info.type==14) {
            R_MO2(s8,actor+0x3A1)=health;
            if(info.type==14 && R_MO2(s8,actor+0x2D59)==0) {
                R_MO2(s8,actor+0x2D59)=1;R_MO2(s16,actor+0x8C2)=8;R_MO2(s16,actor+0x8C0)=-10;
            }
        }
        f32 position[3];position[0]=R_MO2(f32,actor+0x314);position[1]=R_MO2(f32,actor+0x318);
        const f32 z=R_MO2(f32,actor+0x31C);R_MO2(s16,actor+0x85A)=25;position[2]=z;
        randomizer_mo2_sound_influence((u32)position,100,actor?R_MO2(u32,actor+4):0xFFFFFFFFu,5);
        if(info.type==12)R_MO2(s16,actor+0x858)=10;
        R_MO2(u16,actor+0x2D68)|=info.cut;
        if(info.type==10) {
            R_MO2(s8,actor+0x2E0F)=(s8)(WWHD_GAME_DATA(0x1047BDDE,s16)+8);
            info.damage=1;reaction=info.dead?7:4;
            randomizer_mo2_rotate_y_set(randomizer_mo2_matrix(),info.angle[1]);
        } else {
            R_MO2(s8,actor+0x2E0F)=0;
            if(info.type==1) {
                s16 angle=(s16)(R_MO2(s16,actor+0x87E)-R_MO2(s16,actor+0x322));
                if(angle<0)angle=(s16)-angle;
                if(R_MO2(u8,player+0x3AC)==5)reaction=2;
                else if((u16)angle>0x4000)reaction=info.dead?3:(hit_part==1?4:6);
                else reaction=info.dead?1:(hit_part==1?4:5);
            } else {
                reaction=info.type==2?7:(info.dead?7:4);
                randomizer_mo2_rotate_y_set(randomizer_mo2_matrix(),info.angle[1]);
            }
        }
    }
    if(reaction && R_MO2(u8,actor+0x3D8)==1) {
        if(WWHD_GAME_DATA(0x101BA692,u8)) {
            R_MO2(s16,actor+0x8C2)=6;R_MO2(s16,actor+0x8C0)=-10;
            reaction=5;R_MO2(s16,actor+0x858)=50;
        } else {R_MO2(u8,actor+0x3D8)=0;R_MO2(u8,actor+0x2E0D)=1;}
    }
    switch(reaction) {
    case 1:case 2:case 3:
        randomizer_mo2_texture(actor,4);
        R_MO2(s32,actor+0xD14)|=reaction==1?0x10:0x40;
        if(reaction==2) {
            randomizer_mo2_rotate_y_set(randomizer_mo2_matrix(),(s16)(R_MO2(s16,actor+0x322)+0x8000));
            R_MO2(f32,actor+0xD18)=26.0f;
        } else {
            R_MO2(f32,actor+0xD18)=26.0f;
            randomizer_mo2_rotate_y_set(randomizer_mo2_matrix(),R_MO2(s16,actor+0x87E));
        }
        break;
    case 4:
        randomizer_mo2_texture(actor,1);R_MO2(f32,actor+0xD18)=23.0f;
        R_MO2(s32,actor+0xD14)|=0x10;
        randomizer_mo2_rotate_y_set(randomizer_mo2_matrix(),R_MO2(s16,actor+0x87E));break;
    case 5:
        randomizer_mo2_texture(actor,1);R_MO2(s32,actor+0xD14)|=0x20;
        R_MO2(f32,actor+0xD18)=23.0f;
        randomizer_mo2_rotate_y_set(randomizer_mo2_matrix(),R_MO2(s16,actor+0x87E));
        randomizer_mo2_animation(actor,0x6A,2.0f,0,1.0f,0x31);
        R_MO2(u8,actor+0xFFE)=7;R_MO2(s16,actor+0xD7A)=10;
        R_MO2(f32,actor+0xD64)=5000.0f;
        if(R_MO2(s16,actor+0x882)==0){R_MO2(s16,actor+0x882)=3;R_MO2(s16,actor+0x890)=0;}
        break;
    case 6:
        randomizer_mo2_texture(actor,1);R_MO2(s16,actor+0x858)=200;
        R_MO2(s16,actor+0x8C2)=13;R_MO2(s16,actor+0x8C0)=0;
        if(R_MO2(s16,actor+0x882)==0){R_MO2(s16,actor+0x882)=10;R_MO2(s16,actor+0x890)=-0x4000;}
        break;
    case 7:
        randomizer_mo2_texture(actor,4);R_MO2(s32,actor+0xD14)|=0x10;R_MO2(f32,actor+0xD18)=26.0f;break;
    }
    if(R_MO2(s32,actor+0xD14)!=0) {
        f32 vector[3]={0.0f,0.0f,-10.0f};
        randomizer_mo2_transform((u32)vector,actor+0xD1C);
        if(R_MO2(f32,actor+0xD18)<25.0f)R_MO2(f32,actor+0xDC4)=-WWHD_GAME_DATA(0x10465618,f32);
        else R_MO2(f32,actor+0xD18)=(f32)(randomizer_mo2_random(10.0)+70.0);
    }
    if(reaction) {
        if(R_MO2(s8,actor+0x3A1)<=0 && info.dead) {
            randomizer_mo2_voice(actor,0x480E,0);
            if(R_MO2(u32,actor+0x2E0)&0x04000000)R_MO2(s8,actor+0x2E21)=0x32;
        } else randomizer_mo2_voice(actor,reaction==6?0x480A:0x4809,0);
    }
    return info.damage;
}
