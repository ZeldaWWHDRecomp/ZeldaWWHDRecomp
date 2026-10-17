// SPDX-License-Identifier: MPL-2.0
#include "mods/story_prop_rewards.h"
#ifdef STORY_PROP_REWARDS_RUNTIME_TEST
#include "mods/story_items.h"
#include "mods/story_store.h"
#include "runtime.h"
#include <filesystem>
#include <fstream>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
static std::vector<std::string> diagnostic_logs;
void log_msg(const char* format, ...) {
    char buffer[2048]; va_list args; va_start(args, format);
    std::vsnprintf(buffer, sizeof buffer, format, args); va_end(args);
    diagnostic_logs.emplace_back(buffer);
}
#endif
#include <cassert>
#include <iostream>
#include <map>
#include <stdexcept>
using namespace story_items;
using namespace story_items::prop_rewards;
#ifdef STORY_PROP_REWARDS_RUNTIME_TEST
static void runtime_contract() {
#ifdef _WIN32
    _putenv_s("WWHD_NATIVE_ACTOR_TRACE", "1");
#else
    setenv("WWHD_NATIVE_ACTOR_TRACE", "1", 1);
#endif
    auto map = [](uint32_t at, uint32_t bytes) {
#ifdef _WIN32
        return VirtualAlloc(mem::ptr(at), bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
        return mmap(mem::ptr(at), bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
#endif
    };
    assert(map(0x101F0000,0x10000) && map(0x20000000,0x10000));
    st32(0x101F84DC,0x20000000); story_store::set_memory(0x2000E000);
    auto dir = std::filesystem::temp_directory_path() / "wwhd_prop_rewards_test";
    std::filesystem::create_directories(dir); std::ofstream(dir / "icon.png") << "test icon bytes";
    story_notebook::ManifestItem item; item.id="gem"; item.name="Gem"; item.description="Own gem"; item.icon="icon.png"; item.stack=5;
    std::string why; reset(); assert(define("test",dir.string(),item,why));
    item.id="second"; assert(define("test",dir.string(),item,why));
    const auto checkpoint = prop_marker_checkpoint();
    const auto provisional = register_prop_marker("test","provisional","first",why); assert(provisional);
    // A later invalid declaration fails; caller RAII rolls back the entire startup marker batch.
    assert(!register_prop_marker("test","conflict","first",why));
    std::array<uint8_t,kAreaSize> untouched{}; std::memcpy(untouched.data(),story_store::area(),untouched.size());
    assert(rollback_prop_markers(checkpoint));
    auto h = register_prop_marker("test","pot","broken",why); assert(h && h != provisional && !prop_broken(h));
    assert(claim_prop_drops(provisional,"test",{{"gem",1}},false)==PropClaimStatus::InvalidDefinition);
    assert(!std::memcmp(untouched.data(),story_store::area(),untouched.size()));
    std::array<uint8_t,kAreaSize> before{}; std::memcpy(before.data(),story_store::area(),before.size());
    assert(claim_prop_drops(h,"foreign",{{"gem",1}},false)==PropClaimStatus::InvalidDefinition);
    assert(claim_prop_drops(h,"test",{{"gem",1},{"missing",1}},false)==PropClaimStatus::InvalidDefinition);
    assert(!std::memcmp(before.data(),story_store::area(),before.size()));
    assert(claim_prop_drops(h,"test",{{"gem",2},{"second",3}},false)==PropClaimStatus::Committed);
    assert(prop_broken(h) && count("test","gem")==2 && count("test","second")==3);
    assert(diagnostic_logs.size() == 3);
    assert(diagnostic_logs[0].find("broken=1") != std::string::npos);
    assert(diagnostic_logs[1].find("item=gem ") != std::string::npos && diagnostic_logs[1].find("count=2") != std::string::npos);
    assert(diagnostic_logs[2].find("item=second ") != std::string::npos && diagnostic_logs[2].find("count=3") != std::string::npos);
    assert(!number(item_key("test","gem"))); // no native pool binding
    assert(claim_prop_drops(h,"test",{{"gem",2}},false)==PropClaimStatus::AlreadyBroken);
    std::memcpy(story_store::area(),before.data(),before.size()); // actual snapshot-owned inventory+marker
    assert(!prop_broken(h) && count("test","gem")==0);
    assert(claim_prop_drops(h,"test",{{"gem",6}},false)==PropClaimStatus::StackFull && !prop_broken(h));
    assert(!std::memcmp(before.data(),story_store::area(),before.size()));
    assert(claim_prop_drops(h,"test",{},false)==PropClaimStatus::Committed && prop_broken(h));
    // Enemy markers share monotonic registration, but both lookup and claims are kind checked.
    const auto enemy_checkpoint = prop_marker_checkpoint();
    const auto stale_enemy = register_enemy_marker("test","temporary-enemy","temporary-defeat",why);
    assert(stale_enemy && rollback_prop_markers(enemy_checkpoint));
    const auto enemy = register_enemy_marker("test","keese","defeated",why);
    assert(enemy && enemy.value != stale_enemy.value && enemy.value != h);
    assert(!register_enemy_marker("test","pot","different",why)); // one actor cannot own two kinds
    assert(!enemy_defeated(EnemyMarkerHandle{h}) && !prop_broken(enemy.value));
    assert(claim_enemy_drops(EnemyMarkerHandle{h},"test",{{"gem",1}},false)==EnemyClaimStatus::InvalidDefinition);
    assert(claim_prop_drops(enemy.value,"test",{{"gem",1}},false)==PropClaimStatus::InvalidDefinition);
    assert(claim_enemy_drops(stale_enemy,"test",{},false)==EnemyClaimStatus::InvalidDefinition);
    std::array<uint8_t,kAreaSize> enemy_before{};
    std::memcpy(enemy_before.data(),story_store::area(),enemy_before.size());
    assert(claim_enemy_drops(enemy,"foreign",{{"gem",1}},false)==EnemyClaimStatus::InvalidDefinition);
    assert(claim_enemy_drops(enemy,"test",{{"gem",2},{"missing",1}},false)==EnemyClaimStatus::InvalidDefinition);
    assert(claim_enemy_drops(enemy,"test",{{"gem",0xFFFFFFFFu}},false)==EnemyClaimStatus::InvalidDefinition);
    assert(claim_enemy_drops(enemy,"test",{{"gem",6}},false)==EnemyClaimStatus::StackFull);
    assert(!std::memcmp(enemy_before.data(),story_store::area(),enemy_before.size()));
    const auto logs_before=diagnostic_logs.size();
    assert(claim_enemy_drops(enemy,"test",{{"gem",2},{"second",3}},false)==EnemyClaimStatus::Committed);
    assert(enemy_defeated(enemy) && prop_broken(h) && count("test","gem")==2 && count("test","second")==3);
    assert(diagnostic_logs.size()==logs_before+3 && diagnostic_logs[logs_before].find("[native_enemy_reward]")!=std::string::npos && diagnostic_logs[logs_before].find("defeated=1")!=std::string::npos);
    assert(claim_enemy_drops(enemy,"test",{{"gem",1}},false)==EnemyClaimStatus::AlreadyDefeated);
    std::memcpy(story_store::area(),enemy_before.data(),enemy_before.size());
    assert(!enemy_defeated(enemy) && prop_broken(h) && count("test","gem")==0);
    assert(claim_enemy_drops(enemy,"test",{{"gem",1}},false)==EnemyClaimStatus::Committed);
    // Claims always target the currently loaded Book area, not the previously loaded pointer.
    std::array<uint8_t,kAreaSize> old_loaded{};std::memcpy(old_loaded.data(),story_store::area(),old_loaded.size());
    story_store::set_memory(0x2000D000);
    assert(!enemy_defeated(enemy) && count("test","gem")==0);
    assert(claim_enemy_drops(enemy,"test",{{"gem",1}},false)==EnemyClaimStatus::Committed);
    assert(!std::memcmp(old_loaded.data(),mem::ptr(0x2000E000),old_loaded.size()));
    story_store::set_memory(0x2000E000);
    assert(enemy_defeated(enemy));
    std::filesystem::remove_all(dir);
}
#endif

static void enemy_pure_contract() {
    Registry registry; std::string why;
    // Actual cross-kind same-key collision is safe because Book records include their kind.
    std::map<uint16_t,std::string> prop_keys;
    for(unsigned i=0;i<1000;++i){auto f="p"+std::to_string(i);prop_keys.emplace(story_mail::letter_key("mixed","prop-broken:"+f),f);}
    bool found=false;
    for(unsigned i=0;i<100000&&!found;++i){auto f="e"+std::to_string(i);auto key=story_mail::letter_key("mixed","enemy-defeated:"+f);auto it=prop_keys.find(key);if(it==prop_keys.end())continue;
        auto prop=registry.add("mixed","pot",it->second,why);auto enemy=registry.add("mixed","moblin",f,why,kDefeated);assert(prop&&enemy&&prop!=enemy);
        assert(!registry.get(enemy)&&!registry.get(prop,kDefeated));
        std::array<uint8_t,kAreaSize> area{};story_mail::Book b(area.data(),kRecords);
        auto plan=stage(area.data(),key,{{123,2,5},{124,3,5}},1,kDefeated);assert(plan.status==PropClaimStatus::Committed);
        assert(commit(area.data(),plan,[]{throw std::runtime_error("caption failure");})==PropClaimStatus::Committed);
        assert(b.get(kDefeated,key)==1 && b.get(kBroken,key)==0 && b.get(kRecordItemCount,123)==2 && b.get(kRecordItemCount,124)==3);
        auto prop_plan=stage(area.data(),key,{},1,kBroken);assert(prop_plan.status==PropClaimStatus::Committed);assert(commit(area.data(),prop_plan,[]{})==PropClaimStatus::Committed);assert(b.get(kDefeated,key)==1&&b.get(kBroken,key)==1);
        found=true;
    }
    assert(found);
    const auto cp=registry.checkpoint();auto provisional=registry.add("test","first-enemy","flag",why,kDefeated);assert(provisional);assert(registry.rollback(cp));auto next=registry.add("test","second-enemy","flag",why,kDefeated);assert(next>provisional&&!registry.get(provisional,kDefeated));
    assert(!registry.add("test","second-enemy","flag",why,kBroken));
    std::array<uint8_t,kAreaSize> crowded{};story_mail::Book full(crowded.data(),kRecords);
    for(unsigned i=0;i<254;++i)assert(full.set(77,uint16_t(i+1),1));
    const auto crowded_before=crowded;
    assert(stage(crowded.data(),60000,{{60001,1,5}},1,kDefeated).status==PropClaimStatus::NoRecordCapacity && crowded==crowded_before);
    assert(stage(crowded.data(),60000,{{60001,0xFFFFFFFFu,5}},1,kDefeated).status==PropClaimStatus::InvalidDefinition && crowded==crowded_before);
    std::array<uint8_t,kAreaSize> malformed{};story_mail::Book b(malformed.data(),kRecords);b.set(kDefeated,42,2);auto before=malformed;assert(stage(malformed.data(),43,{},1,kDefeated).status!=PropClaimStatus::Committed && malformed==before);
}

int main() {
    enemy_pure_contract();
#ifdef STORY_PROP_REWARDS_RUNTIME_TEST
    runtime_contract();
#endif
    Registry r; std::string why;
    const auto h = r.add("test", "pot", "broken", why);
    assert(h && r.add("test", "pot", "broken", why) == h);
    assert(!r.add("test", "other", "broken", why));
    assert(!r.add("test", "pot", "different", why));
    assert(!r.get(0) && !r.get(257));
    assert(!r.add("bad/mod", "pot", "flag", why));
    const auto checkpoint = r.checkpoint();
    const auto provisional = r.add("retry", "a", "first", why); assert(provisional);
    assert(!r.add("retry", "b", "first", why));
    assert(r.rollback(checkpoint) && !r.get(provisional) && r.get(h));
    const auto replacement = r.add("retry", "c", "second", why);
    assert(replacement && replacement != provisional && !r.get(provisional));
    assert(!r.rollback(999));
    const auto stable_count = r.checkpoint();
    PropMarkerHandle last = replacement;
    for (unsigned i = 0; i < 300; ++i) {
        auto transient = r.add("transient", "actor", "flag", why);
        assert(transient > last); last = transient;
        assert(r.rollback(stable_count) && !r.get(transient));
    }
    assert(r.get(h) && r.get(replacement));
    // Find a real salted-key collision, rather than depending on a fixed hash constant.
    std::map<uint16_t,std::string> seen; Registry collisions; bool found = false;
    for (unsigned i = 0; i < 100000 && !found; ++i) {
        auto f = "f" + std::to_string(i); auto k = story_mail::letter_key("collision", "prop-broken:" + f);
        if (seen.count(k)) {
            assert(collisions.add("collision", "a", seen[k], why));
            assert(!collisions.add("collision", "b", f, why)); found = true;
        } else seen[k] = f;
    }
    assert(found);
    Registry full;
    // Unique hashed keys avoid conflating registry capacity with collision rejection.
    std::map<uint16_t,bool> keys;
    for (unsigned i = 0; keys.size() < 256; ++i) {
        auto id = "i" + std::to_string(i); auto key = story_mail::letter_key("full", "prop-broken:" + id);
        if (!keys.emplace(key,true).second) continue;
        assert(full.add("full", id, id, why));
    }
    assert(!full.add("extra", "actor", "flag", why));
    std::array<uint8_t,kAreaSize> live{}; auto original = live;
    auto plan = stage(live.data(), r.get(h)->key, {{1,2,5},{2,3,5}}, 4);
    assert(plan.status == PropClaimStatus::Committed && live == original);
    unsigned notifications = 0;
    assert(commit(live.data(), plan, [&]{ ++notifications; throw std::runtime_error("caption unavailable"); }) == PropClaimStatus::Committed);
    assert(notifications == 1);
    story_mail::Book b(live.data(), kRecords); Bag bag(b,4);
    assert(b.get(kBroken,r.get(h)->key) == 1 && bag.count(1) == 2 && bag.count(2) == 3);
    assert(stage(live.data(),r.get(h)->key,{{1,2,5}},4).status == PropClaimStatus::AlreadyBroken);
    // Fullstate restoration is the entire transaction, not host-registry state.
    live = original;
    assert(stage(live.data(),r.get(h)->key,{{1,2,5}},4).status == PropClaimStatus::Committed);
    assert(stage(live.data(),7,{{1,6,5}},4).status == PropClaimStatus::StackFull && live == original);
    assert(stage(live.data(),7,{{1,1,5},{1,1,5}},4).status == PropClaimStatus::InvalidDefinition);
    assert(stage(live.data(),7,{{1,0,5}},4).status == PropClaimStatus::InvalidDefinition);
    plan = stage(live.data(),7,{{1,1,5}},4); live[100] = 1;
    auto changed = live; assert(commit(live.data(),plan,[]{assert(false);}) == PropClaimStatus::Stale && live == changed);
    live = {}; b = story_mail::Book(live.data(),kRecords);
    for (unsigned i = 0; i < kRecords - 1; ++i) assert(b.set(99,uint16_t(i+1),1));
    original = live;
    assert(stage(live.data(),7,{{1,1,5}},4).status == PropClaimStatus::NoRecordCapacity && live == original);
    // Last two records reserve marker+count; optional placement may fail without losing reward.
    assert(b.erase(99,1)); plan = stage(live.data(),7,{{1,1,5}},4);
    assert(plan.status == PropClaimStatus::Committed && !plan.placed[0]);
    assert(commit(live.data(),plan,[]{}) == PropClaimStatus::Committed);
    // Three free records suffice for marker and two counts, even without room for placements.
    live = {}; b = story_mail::Book(live.data(),kRecords);
    for (unsigned i = 0; i < kRecords - 3; ++i) assert(b.set(99,uint16_t(i+1),1));
    plan = stage(live.data(),7,{{1,1,5},{2,1,5}},4);
    assert(plan.status == PropClaimStatus::Committed && !plan.placed[0] && !plan.placed[1]);
    live = {}; live[0] = 0x55;
    assert(stage(live.data(),7,{},4).status == PropClaimStatus::StoreUnavailable);
    live = {}; b = story_mail::Book(live.data(),kRecords); assert(b.set(10,1,255));
    assert(stage(live.data(),7,{},4).status == PropClaimStatus::StoreUnavailable);
    live = {}; b = story_mail::Book(live.data(),kRecords); assert(b.set(99,1,1));
    std::memcpy(live.data()+8,live.data()+4,4);
    assert(stage(live.data(),7,{},4).status == PropClaimStatus::StoreUnavailable);
    live = {}; b = story_mail::Book(live.data(),kRecords); assert(b.set(kRecordItemPlace,1,9));
    assert(stage(live.data(),7,{},4).status == PropClaimStatus::StoreUnavailable);
    live = {}; b = story_mail::Book(live.data(),kRecords); assert(b.set(kRecordItemCount,1,1));
    assert(b.set(kRecordItemCount,2,1) && b.set(kRecordItemPlace,1,9) && b.set(kRecordItemPlace,2,9));
    assert(stage(live.data(),7,{},4).status == PropClaimStatus::StoreUnavailable);
    std::cout << "prop reward registry, atomic batch, capacity, restore, stale and notification contracts passed\n";
}
