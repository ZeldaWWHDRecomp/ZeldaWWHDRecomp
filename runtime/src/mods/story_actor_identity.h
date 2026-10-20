// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "story_world_manifest.h"
#include "mod_hash.h"
#include <array>
#include <bit>
#include <algorithm>
namespace story_actors::identity {
using Fingerprint=std::array<uint8_t,32>;
inline constexpr uint32_t kMagic=0x41544346,kVersion=1,kRecordBytes=40;
// Length-prefixed strings and explicit BE numeric encodings make this independent of
// struct padding, host endianness and locale. Drop/action/path order is significant.
inline Fingerprint fingerprint(const std::string&mod,const story_notebook::ManifestActor&a){
    std::string bytes;auto word=[&](uint32_t v){for(int shift=24;shift>=0;shift-=8)bytes.push_back(char(v>>shift));};
    auto text=[&](const std::string&s){word(uint32_t(s.size()));bytes+=s;};
    auto number=[&](float v){word(std::bit_cast<uint32_t>(v));};
    auto point=[&](const story_notebook::WorldPoint&p){number(p.x);number(p.y);number(p.z);};
    text("wwhd.actor-config.v1");text(mod);
    for(const auto*s:{&a.id,&a.actor,&a.stage,&a.kind,&a.model,&a.animation,&a.prompt,&a.broken_flag,&a.switch_flag,&a.switch_actor,&a.defeated_flag,&a.flag,&a.quest,&a.item,&a.time,&a.lines})text(*s);
    word(a.room);point(a.position);point(a.rotation);point(a.scale);word(a.params);word(a.resident);word(a.item_game);word(a.quest_step);
    number(a.collision_radius);number(a.collision_height);word(a.pushable);
    const auto&look=a.look_at;word(look.enabled);text(look.head);text(look.upper_bone);number(look.yaw_degrees);number(look.pitch_degrees);number(look.rate_degrees);number(look.radius);number(look.upper_yaw_degrees);number(look.upper_fraction);
    number(a.path.speed);word(a.path.loop);word(uint32_t(a.path.points.size()));for(const auto&p:a.path.points)point(p);
    if(a.path.back_and_forth||a.path.pauses||!a.path.speed_given||!a.path.walk_clip.empty()||!a.path.idle_clip.empty()){text("wwhd.actor-path.v2");word(a.path.back_and_forth);word(a.path.pauses);word(a.path.speed_given);text(a.path.walk_clip);text(a.path.idle_clip);}
    word(uint32_t(a.drops.size()));for(const auto&d:a.drops){text(d.item);word(d.quantity);}
    word(uint32_t(a.then.size()));for(const auto&action:a.then){word(uint32_t(action.kind));text(action.id);word(action.value);}
    if(!a.when_flag.empty()||!a.when_not_flag.empty()||!a.quests.empty()){text("wwhd.actor-conditions.v1");text(a.when_flag);text(a.when_not_flag);word(uint32_t(a.quests.size()));for(const auto&q:a.quests){text(q.quest);word(q.step);}}
    if(!a.talks.empty()){text("wwhd.actor-talks.v1");word(uint32_t(a.talks.size()));for(const auto&t:a.talks){
        text(t.quest);word(t.quest_step);text(t.item);text(t.time);text(t.when_flag);text(t.when_not_flag);text(t.lines);
        word(uint32_t(t.quests.size()));for(const auto&q:t.quests){text(q.quest);word(q.step);}
        word(uint32_t(t.then.size()));for(const auto&action:t.then){word(uint32_t(action.kind));text(action.id);word(action.value);}
    }}
    if(a.combat_enabled){
        text("wwhd.mod-combat.v1");const auto&c=a.combat;word(c.health);word(c.invulnerability_frames);word(c.target_priority);word(a.native_heart_drop);
        for(const auto*r:{&c.flinch,&c.stun,&c.death}){text(r->clip);word(r->frames);number(r->knockback_speed);word(r->knockback_frames);}
        const auto&at=c.attack;word(at.damage);word(at.active_frames);word(at.cooldown_frames);number(at.radius);number(at.height);number(at.forward_offset);number(at.y_offset);
    }
    if(!a.variant.empty()){text("wwhd.actor-variant.v1");text(a.variant);}
    if(!a.hole.empty()){text("wwhd.actor-rat-hole.v1");text(a.hole);word(a.rat_bomb);}
    if(!a.defeat_switch.empty()){text("wwhd.actor-defeat-switch.v1");text(a.defeat_switch);}
    if(a.world_object()){text("wwhd.world-object.v1");text(a.behaviour);text(a.collision_mesh);text(a.collision_shape);text(a.collision_surface);point(a.collision_size);}
    if(a.water_object()){text("wwhd.world-motion.v1");number(a.buoyancy.draft);number(a.buoyancy.damping);number(a.buoyancy.max_tilt);number(a.drift.wind_speed);number(a.drift.max_speed);point(a.drift.current);word(a.anchor.enabled);point(a.anchor.point);number(a.anchor.radius);}
    if(!a.grapple_points.empty()){text("wwhd.world-grapple.v1");word(uint32_t(a.grapple_points.size()));for(const auto&p:a.grapple_points){text(p.id);point(p.position);number(p.yaw);}}
    if(!a.distant.model.empty()||!a.shoreline.model.empty()||a.chart_marker.enabled||a.arrival.enabled){
        text("wwhd.world-island.v1");text(a.distant.model);text(a.shoreline.model);if(a.shoreline.material=="water")text("shoreline-water");else if(!a.shoreline.material.empty())text("shoreline-"+a.shoreline.material);word(a.chart_marker.enabled);text(a.chart_marker.label);text(a.chart_marker.when_flag);word(a.arrival.enabled);text(a.arrival.name);text(a.arrival.flag);number(a.arrival.radius);
    }
    if(a.carry_enabled){text("wwhd.mod-carry.v1");word(a.carry_break_on_impact);}
    if(!a.texture.empty()){text("wwhd.actor-texture.v1");text(a.texture);text(a.texture_digest);}
    const auto hex=mods::hash::sha256_text(bytes);Fingerprint out{};
    const auto digit=[](char c){return uint8_t(c<='9'?c-'0':c-'a'+10);};for(size_t i=0;i<out.size();++i)out[i]=uint8_t(digit(hex[i*2])*16+digit(hex[i*2+1]));return out;
}
// The selected branch and the complete immutable declaration bind restoration;
// changing owned flag/quest VALUES does not change this definition identity.
inline Fingerprint selected_talk(const std::string&mod,const story_notebook::ManifestActor&a,unsigned choice){
    auto hash=fingerprint(mod,a);std::string bytes("wwhd.selected-talk.v1");
    bytes.append(reinterpret_cast<const char*>(hash.data()),hash.size());
    for(int shift=24;shift>=0;shift-=8)bytes.push_back(char(choice>>shift));
    const auto hex=mods::hash::sha256_text(bytes);Fingerprint out{};
    auto digit=[](char c){return uint8_t(c<='9'?c-'0':c-'a'+10);};
    for(size_t i=0;i<out.size();++i)out[i]=uint8_t(digit(hex[2*i])*16+digit(hex[2*i+1]));return out;
}
inline std::array<uint8_t,kRecordBytes> record(const Fingerprint&hash){std::array<uint8_t,kRecordBytes> out{};for(unsigned i=0;i<4;++i){out[i]=uint8_t(kMagic>>(24-i*8));out[i+4]=uint8_t(kVersion>>(24-i*8));}std::copy(hash.begin(),hash.end(),out.begin()+8);return out;}
inline bool matches(const uint8_t*saved,const Fingerprint&hash){if(!saved)return false;const auto expected=record(hash);return std::equal(expected.begin(),expected.end(),saved);}
}
