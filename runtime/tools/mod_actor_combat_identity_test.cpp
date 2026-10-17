// SPDX-License-Identifier: MPL-2.0
#include "mods/story_actor_identity.h"
#include <cassert>
#include <functional>
#include <string>
#include <utility>
#include <vector>
using story_notebook::ManifestActor;
using story_actors::identity::fingerprint;
namespace {
ManifestActor fixture(){
    ManifestActor a;a.id="guard";a.kind="mod";a.model="own-rig";a.stage="sea";a.room=11;a.position={100,1040,-201500};a.combat_enabled=true;a.defeated_flag="guard.defeated";a.drops={{"token",1},{"seal",2}};
    auto& c=a.combat;c.health=5;c.invulnerability_frames=7;c.target_priority=1;c.flinch={"flinch",8,100,4};c.stun={"stun",30,0,0};c.death={"death",15,0,0};c.attack={2,3,12,35,120,45,10};return a;
}
}
int main(){
    const auto base=fixture();const auto expected=fingerprint("owner",base);
    std::vector<std::pair<std::string,std::function<void(ManifestActor&)>>> changes={
        {"enabled",[](auto&a){a.combat_enabled=false;}},
        {"health",[](auto&a){++a.combat.health;}},
        {"invulnerability",[](auto&a){++a.combat.invulnerability_frames;}},
        {"priority low",[](auto&a){a.combat.target_priority=0;}},
        {"priority high",[](auto&a){a.combat.target_priority=2;}},
        {"native heart",[](auto&a){a.native_heart_drop=true;}},
        {"defeat flag",[](auto&a){a.defeated_flag="other.defeated";}},
        {"drop item",[](auto&a){a.drops[0].item="other-token";}},
        {"drop quantity",[](auto&a){++a.drops[0].quantity;}},
        {"drop order",[](auto&a){std::swap(a.drops[0],a.drops[1]);}},
        {"drop removal",[](auto&a){a.drops.pop_back();}},
        {"attack damage",[](auto&a){++a.combat.attack.damage;}},
        {"active duration",[](auto&a){++a.combat.attack.active_frames;}},
        {"cooldown",[](auto&a){++a.combat.attack.cooldown_frames;}},
        {"radius",[](auto&a){a.combat.attack.radius+=.5f;}},
        {"height",[](auto&a){a.combat.attack.height+=.5f;}},
        {"forward offset",[](auto&a){a.combat.attack.forward_offset+=.5f;}},
        {"vertical offset",[](auto&a){a.combat.attack.y_offset=-a.combat.attack.y_offset;}},
    };
    for(unsigned reaction=0;reaction<3;++reaction){
        auto field=[reaction](ManifestActor&a)->story_mod_combat::Reaction&{return reaction==0?a.combat.flinch:reaction==1?a.combat.stun:a.combat.death;};
        changes.push_back({"reaction clip",[field](auto&a){field(a).clip+="-changed";}});
        changes.push_back({"reaction duration",[field](auto&a){++field(a).frames;}});
        changes.push_back({"reaction knockback speed",[field](auto&a){field(a).knockback_speed+=1;}});
        changes.push_back({"reaction knockback duration",[field](auto&a){++field(a).knockback_frames;}});
    }
    for(const auto& [name,change]:changes){auto changed=base;change(changed);assert(fingerprint("owner",changed)!=expected);}
    assert(fingerprint("other-owner",base)!=expected);
    // A captured record from the original declaration cannot authenticate any
    // altered combat contract, even if the actor token and native PID repeat.
    const auto saved=story_actors::identity::record(expected);assert(story_actors::identity::matches(saved.data(),expected));
    for(const auto& [name,change]:changes){auto changed=base;change(changed);assert(!story_actors::identity::matches(saved.data(),fingerprint("owner",changed)));}
    auto corrupt=saved;corrupt[0]^=1;assert(!story_actors::identity::matches(corrupt.data(),expected));corrupt=saved;corrupt[7]^=1;assert(!story_actors::identity::matches(corrupt.data(),expected));corrupt=saved;corrupt[39]^=1;assert(!story_actors::identity::matches(corrupt.data(),expected));
    // Length-prefixed clip strings must not collapse equal concatenations.
    auto one=base,two=base;one.combat.flinch.clip="a";one.combat.stun.clip="bc";two.combat.flinch.clip="ab";two.combat.stun.clip="c";assert(fingerprint("owner",one)!=fingerprint("owner",two));
    // The extension is conditional: legacy noncombat actors keep their original
    // digest regardless of every unused combat default or native-heart field.
    auto legacy=base;legacy.combat_enabled=false;const auto legacy_hash=fingerprint("owner",legacy);
    for(const auto& [name,change]:changes){auto changed=legacy;change(changed);changed.combat_enabled=false;if(name=="defeat flag"||name.starts_with("drop "))assert(fingerprint("owner",changed)!=legacy_hash);else assert(fingerprint("owner",changed)==legacy_hash);}
    auto unused=legacy;unused.combat={};unused.combat.health=255;unused.combat.invulnerability_frames=900;unused.combat.target_priority=2;unused.combat.attack={4,150,900,200,400,300,-200};unused.combat.flinch={"unused-a",900,1500,30};unused.combat.stun={"unused-b",899,42,3};unused.combat.death={"unused-c",898,43,4};unused.native_heart_drop=true;assert(fingerprint("owner",unused)==legacy_hash);
    auto unused_carry=legacy;unused_carry.carry_break_on_impact=true;assert(fingerprint("owner",unused_carry)==legacy_hash);
    auto carry=legacy;carry.carry_enabled=true;const auto carry_hash=fingerprint("owner",carry);assert(carry_hash!=legacy_hash);
    carry.carry_break_on_impact=true;const auto breaking_hash=fingerprint("owner",carry);assert(breaking_hash!=carry_hash);
    const auto carry_record=story_actors::identity::record(carry_hash);assert(story_actors::identity::matches(carry_record.data(),carry_hash));
    assert(!story_actors::identity::matches(carry_record.data(),breaking_hash));
}
