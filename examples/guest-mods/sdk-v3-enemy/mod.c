/* SPDX-License-Identifier: MPL-2.0
 * Original bounded patrol example. Combat state and damage are runtime owned. */
#include "wwhd_guest.h"
#include "wwhd/actor.h"
#include "wwhd/bindings.h"
typedef struct {u32 handle,walking,last_phase,last_sequence,last_link_sample;} Enemy;
static wwhd_actor_state_v1 state;
static wwhd_actor_combat_state_v1 combat;
static const wwhd_actor_vec3 route[2]={{800,1040,-201500},{940,1040,-201500}};
static wwhd_actor_vec3 link_position;
static u32 registered,link_valid,link_sample;
static f32 absolute(f32 n){return n<0?-n:n;}
static u32 bounded(f32 n){return n==n&&n>=-1000000&&n<=1000000;}
static void create(wwhd_actor_handle handle,void* data,u32 reason){
    Enemy* e=(Enemy*)data;e->handle=handle;link_valid=0; /* Wait for fresh native Link input after a restore. */
    if(reason==WWHD_ACTOR_NEW){e->walking=0;e->last_phase=0;e->last_sequence=0;e->last_link_sample=0;}
    wwhd_log(reason==WWHD_ACTOR_RESTORED?"patrol enemy restored":"patrol enemy new");
}
static void execute(wwhd_actor_handle handle,void* data,f32 dt){
    Enemy* e=(Enemy*)data;state.size=sizeof state;state.version=1;combat.size=sizeof combat;combat.version=1;
    if(!(dt>0&&dt<=.25f)||!wwhd_actor_get(handle,&state)||!wwhd_actor_combat_get(handle,&combat))return;
    if(combat.health!=0&&e->last_sequence!=combat.last_damage_sequence){e->last_sequence=combat.last_damage_sequence;wwhd_log_int("patrol health",(s32)combat.health);}
    if(e->last_phase!=combat.phase){e->last_phase=combat.phase;wwhd_log_int("patrol combat phase",(s32)combat.phase);}
    if((state.flags&WWHD_ACTOR_SCENE_OWNED)||(combat.flags&WWHD_COMBAT_FLAG_QUARANTINED)||combat.phase!=WWHD_COMBAT_ALIVE){
        wwhd_actor_attack_cancel(handle);if(e->walking)wwhd_actor_stop(handle);e->walking=0;return;
    }
    /* Copied values, never a retained native pointer. Serial requires a fresh
     * native Link Execute sample; create clears validity on NEW and RESTORED. */
    if(!link_valid||e->last_link_sample==link_sample){wwhd_actor_attack_cancel(handle);return;}
    e->last_link_sample=link_sample;
    f32 dx=link_position.x-state.position.x,dz=link_position.z-state.position.z,dy=link_position.y-state.position.y;
    if(dx*dx+dz*dz<=140.f*140.f&&absolute(dy)<=100.f){
        if(e->walking)wwhd_actor_stop(handle);e->walking=0;
        f32 yaw=absolute(dx)>absolute(dz)?(dx>=0?90.f:-90.f):(dz>=0?0.f:180.f);
        wwhd_actor_set_rotation(handle,0,yaw,0);
        if(!combat.attack_active_frames){wwhd_actor_set_animation(handle,"idle",.1f,1);if(wwhd_actor_attack_begin(handle))wwhd_log("patrol attack begin");}
    }else{
        wwhd_actor_attack_cancel(handle);
        if(!e->walking&&wwhd_actor_walk(handle,route,2,45,1)){e->walking=1;wwhd_actor_set_animation(handle,"walk",.2f,1);}
    }
}
static void on_hit(wwhd_actor_handle handle,void* data,const wwhd_actor_hit_v1* hit){
    (void)handle;(void)data;
    if(hit->size==64&&hit->version==1)wwhd_log_int("patrol informational contact",(s32)hit->sequence);
    /* Do not decrement health or replace runtime flinch/stun/death animations. */
}
static const wwhd_actor_behavior_v1 behavior={32,1,sizeof(Enemy),create,execute,0,0,on_hit};
WWHD_HOOK(WWHD_ADDR_daPy_Execute,sample_link,(void* link)){
    const fopAc_ac_c* actor=(const fopAc_ac_c*)link;
    if(!actor){link_valid=0;return;}
    link_position.x=actor->current.pos.x;link_position.y=actor->current.pos.y;link_position.z=actor->current.pos.z;
    link_valid=bounded(link_position.x)&&bounded(link_position.y)&&bounded(link_position.z);
    if(++link_sample==0)link_sample=1;
    if(!registered&&wwhd_actor_register("patrol-enemy",&behavior))registered=1;
}
