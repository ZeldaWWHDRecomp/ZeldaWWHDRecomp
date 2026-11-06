// Run the production cheat service against synthetic guest memory. No game files.
#include "runtime.h"
#include "mods/mods.h"
#include "input.h"
#include "mods/save_backup.h"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <array>
#include <cassert>
#include <cstring>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#endif
void log_msg(const char*, ...) {}
namespace mods { uint64_t step() { return 1000; } }
static std::string last_notice;
namespace ss { void notice(const std::string& text) { last_notice=text; } }
uint32_t guest_call(Cpu* cpu, uint32_t address, std::initializer_list<uint32_t> args) {
    assert(address == 0x023F6564 && args.size() == 2);
    const uint32_t link = *args.begin();
    st32(link + 0x65F0, 0x27);
    st32(link + 0x6A70, 2);
    cpu->r[3] = 0xBAD; // The hook must preserve the caller's registers.
    return 0;
}
static void map(uint32_t address, size_t size) {
#ifdef _WIN32
    auto p = VirtualAlloc(mem::ptr(address), size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    auto p = mmap(mem::ptr(address), size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
    assert(p == mem::ptr(address));
}
int main() {
    map(0x101C0000, 0x10000);
    map(0x101F0000, 0x10000);
    map(0x10470000, 0x10000);
    constexpr uint32_t base = 0x15000000, player = base + 0x20;
    map(base, 0x10000);
    st32(0x101F84DC, base);
    std::memcpy(mem::ptr(0x104741E4), "Atorizk", 8);
    // Each earned sword phase, including before Medli and between the two temples.
    for (uint8_t sword : {1, 3, 7, 15}) {
        std::memset(mem::ptr(base), 0xA5, 0x2000);
        st16(player, 14); st16(player + 2, 14); st16(player + 4, 158);
        st8(player + 0x12, 0);
        std::memcpy(mem::ptr(player + 0x30), "sea\0\0\0\0\0", 8);
        st8(player + 0xB4, sword); st8(player + 0xB5, 1);
        std::array<uint8_t, 0x2000> expected;
        std::memcpy(expected.data(), mem::ptr(base), expected.size());
        expected[0x2E] = 0x3E; expected[0x2F] = 0x3C;
        mods::request_cheat(mods::kCheatSword);
        mods::cheats_service();
        // Includes all story flags, ownership bits, adjacent memory, and the save header.
        assert(!std::memcmp(expected.data(), mem::ptr(base), expected.size()));
        mods::cheats_service();
        assert(!std::memcmp(expected.data(), mem::ptr(base), expected.size()));
    }
    // Fill the bags, infinite rupees / bait and spoils / swimming.
    {
        std::memset(mem::ptr(base), 0xFF, 0x2000);
        st16(player, 14); st16(player + 2, 14); st16(player + 4, 150);
        st8(player + 0x12, 1);  // 1000 wallet
        st8(player + 0x13, 0); st8(player + 0x14, 0);
        std::memcpy(mem::ptr(player + 0x30), "sea\0\0\0\0\0", 8);
        st8(player + 0x76, 0x47); st8(player + 0x76 + 3, 0x1F);  // two spoils already in the bag
        st8(player + 0x9C + 2, 5);
        st8(player + 0x94, 0); st8(player + 0x95, 0);
        mods::request_cheat(mods::kCheatBags);
        mods::cheats_service();
        assert(ld8(player + 0x3C + 4) == 0x24 && ld8(player + 0x3C + 11) == 0x2C);  // both bags given
        int spoils = 0;
        for (int k = 0; k < 8; k++) {
            const int item = ld8(player + 0x76 + k);
            spoils |= item == 0x1F ? 0x80 : item >= 0x45 && item <= 0x4B ? 1 << (item - 0x45) : 0x100;
        }
        assert(spoils == 0xFF);  // every kind of spoils once (Joy Pendant 0x1F counted as bit 7)
        assert(ld8(player + 0x76) == 0x47 && ld8(player + 0x79) == 0x1F);  // existing slots kept
        for (int k = 0; k < 8; k++) assert(ld8(player + 0x9C + k) == 99);
        for (int k = 0; k < 8; k++) assert(ld8(player + 0x7E + k) == (k < 4 ? 0x82 : 0x83) && ld8(player + 0xA4 + k) == 3);
        assert(ld8(player + 0x94) == 0xFF && ld8(player + 0x95) == 3);

        constexpr uint32_t rupee_change = 0x1046F0B0 + 0x5B48, air = 0x1046F0B0 + 0x5B4C, play_select = 0x1046F0B0 + 0x5BBB;
        st32(rupee_change, 0); st32(air, 0); st32(air + 4, 900);
        mods::set_infinite(mods::kInfRupees, true);
        mods::cheats_service();
        assert(ld32(rupee_change) == 850);  // counts up to the full 1000 wallet through the HUD
        st16(player + 4, 1000); st32(rupee_change, uint32_t(-200));  // a shop charge not taken yet
        mods::cheats_service();
        assert(ld32(rupee_change) == 0 && ld16(player + 4) == 1000);
        mods::set_infinite(mods::kInfRupees, false);
        st32(rupee_change, uint32_t(-20));
        mods::cheats_service();
        assert(ld32(rupee_change) == uint32_t(-20));  // off: untouched

        mods::set_infinite(mods::kInfBreath, true);
        mods::cheats_service();
        assert(ld32(air) == 0);  // not swimming: the meter stays hidden
        st32(air, 300);
        mods::cheats_service();
        assert(ld32(air) == 900);
        mods::set_infinite(mods::kInfBreath, false);

        st8(player + 0x09 + 1, 36 + 5);  // Y holds the Hyoi Pear in bait slot 5
        mods::set_infinite(mods::kInfBags, true);
        mods::cheats_service();  // remembers the bags
        st8(player + 0x9C + 1, 3); st8(player + 0xA4 + 0, 2);  // a seed and one bait used
        st8(player + 0x7E + 5, 0xFF); st8(player + 0xA4 + 5, 0); st8(player + 0x09 + 1, 0xFF);  // the pear used up
        mods::cheats_service();
        assert(ld8(player + 0x9C + 1) == 99 && ld8(player + 0xA4) == 3);
        assert(ld8(player + 0x7E + 5) == 0x83 && ld8(player + 0x09 + 1) == 36 + 5 && ld8(play_select + 1) == 0x83);
        mods::set_infinite(mods::kInfBags, false);
        st8(player + 0xA4, 1);
        mods::cheats_service();
        assert(ld8(player + 0xA4) == 1);  // off: untouched
    }
    // All figurines: the 134 bits of l_figure_comp's registers (GameCube decompilation d_a_npc_mt.cpp),
    // nothing else of the event flags.
    {
        static const uint16_t regs[17] = {0x95FF, 0x94FF, 0x93FF, 0x92FF, 0x91FF, 0x90FF, 0x8FFF, 0x8EFF, 0x8DFF,
                                          0x8CFF, 0xB1FF, 0x9CFF, 0x84FF, 0x83FF, 0x82FF, 0x81FF, 0x80FF};
        for (int i = 0; i < 17; i++) st16(0x101C2478 + 2 * i, regs[i]);
        std::memset(mem::ptr(base), 0, 0x2000);
        st16(player, 14); st16(player + 2, 14); st16(player + 4, 150);
        std::memcpy(mem::ptr(player + 0x30), "sea\0\0\0\0\0", 8);
        constexpr uint32_t event = base + 0x644;
        st8(event + 0x93, 0x10);  // one figurine already made
        // not a member yet (event 0x3401 off): nothing changes
        std::array<uint8_t, 0x2000> before;
        std::memcpy(before.data(), mem::ptr(base), before.size());
        mods::request_cheat(mods::kCheatFigurines);
        mods::cheats_service();
        assert(!std::memcmp(before.data(), mem::ptr(base), before.size()));
        st8(event + 0x34, 0x01);  // a member: Carlov took a picto
        std::array<uint8_t, 0x2000> expected;
        std::memcpy(expected.data(), mem::ptr(base), expected.size());
        for (int i = 0; i < 17; i++) expected[0x644 + (regs[i] >> 8)] = i == 16 ? 0x3F : 0xFF;
        expected[0x644 + 0x3A] |= 0x01;
        mods::request_cheat(mods::kCheatFigurines);
        mods::cheats_service();
        assert(!std::memcmp(expected.data(), mem::ptr(base), expected.size()));
        int made = 0;
        for (int f = 0; f < 0x86; f++) made += (ld8(event + (regs[f / 8] >> 8)) >> (f % 8)) & 1;
        assert(made == 0x86);  // daNpcMt_c::getFigureMakeNum
    }
    constexpr uint32_t link = 0x16000000, camera = 0x17000000;
    map(link, 0x10000); map(camera, 0x10000);
    Cpu cpu{}; cpu.r[3] = link;
    float x = 7, z = 9;
    // A disabled cheat must not read an unmapped actor or alter output arguments.
    mods::moon_jump_step(&cpu, 0x18000000, true);
    assert(!mods::moon_jump_move(0x18000000, x, z) && x == 7 && z == 9);
    mods::set_infinite(mods::kMoonJump, true);
    mods::moon_jump_step(&cpu, link, true); // consume the switch's reset request
    mods::moon_jump_step(&cpu, link, false);
    // the moon jump's own input (input_map kMoonJump), never the game's L
    input::PadState pad{}; pad.virt = input::kVirtMoonJump; pad.lx = -1;
    {  // the game's L alone: no moon jump
        input::PadState l{}; l.buttons = input::kL; mods::moon_jump_input(l);
        st32(link + 0x65F0, 0x04);
        mods::moon_jump_step(&cpu, link, true);
        assert(ld32(link + 0x65F0) == 0x04);
        mods::moon_jump_step(&cpu, link, false);
    }
    mods::moon_jump_input(pad);
    stf32(camera + 0xF0, 1); // camera facing +Z
    mods::moon_jump_camera(camera);
    st32(link + 0x834, 0xFFFF); stf32(link + 0x318, 100);
    stf32(link + 0x6A48, 999);
    mods::moon_jump_step(&cpu, link, true);
    assert(cpu.r[3] == link && ld32(link + 0x65F0) == 0x27);
    assert((ld32(link + 0x834) & 0xE0) == 0 && ldf32(link + 0x340) == 25);
    assert(ldf32(link + 0x6A48) == 999); // optional fall protection is off
    assert(mods::moon_jump_move(link, x, z) && ld16(link + 0x322) == 3000);
    mods::moon_jump_step(&cpu, link, false);
    mods::moon_jump_step(&cpu, link, true);  // the next logic step turns on
    assert(mods::moon_jump_move(link, x, z) && ld16(link + 0x322) == 6000);
    mods::moon_jump_step(&cpu, link, false);
    mods::set_infinite(mods::kMoonNoFall, true);
    pad.virt = 0; mods::moon_jump_input(pad);
    mods::moon_jump_step(&cpu, link, true);
    assert(ldf32(link + 0x6A48) == 100);
    mods::moon_jump_step(&cpu, link, false);
    st32(link + 0x6A70, 0); // landing ends steering and fall protection
    mods::moon_jump_step(&cpu, link, true);
    assert(!mods::moon_jump_move(link, x, z));
    mods::moon_jump_step(&cpu, link, false);
    stf32(link + 0x6A48, 777);
    mods::moon_jump_step(&cpu, link, true);
    mods::moon_jump_step(&cpu, link, false);
    assert(ldf32(link + 0x6A48) == 777); // ordinary falls are not protected
    // Even when the allocator reuses Link's address, a scene change invalidates
    // the old camera until the new scene has drawn one.
    std::memcpy(mem::ptr(0x104741E4), "Other\0\0\0", 8);
    pad.virt = input::kVirtMoonJump; mods::moon_jump_input(pad);
    mods::moon_jump_step(&cpu, link, true);
    assert(mods::moon_jump_move(link, x, z) && x == 0 && z == 0);
    mods::moon_jump_step(&cpu, link, false);
    mods::moon_jump_camera(camera);
    mods::moon_jump_step(&cpu, link, true);
    assert(mods::moon_jump_move(link, x, z) && (x != 0 || z != 0));
    mods::moon_jump_step(&cpu, link, false);
    auto invalid_transition = [&](bool title_draw) {
        std::memcpy(mem::ptr(0x104741E4), "Name\0\0\0\0", 8);
        mods::moon_jump_step(&cpu, link, true);
        mods::moon_jump_step(&cpu, link, false);
        if (title_draw) mods::moon_jump_camera(camera);
        std::memcpy(mem::ptr(0x104741E4), "Other\0\0\0", 8);
        mods::moon_jump_input(pad);
        mods::moon_jump_step(&cpu, link, true);
        assert(mods::moon_jump_move(link, x, z) && x == 0 && z == 0);
        mods::moon_jump_step(&cpu, link, false);
        mods::moon_jump_camera(camera);
        mods::moon_jump_step(&cpu, link, true);
        assert(mods::moon_jump_move(link, x, z) && (x != 0 || z != 0));
        mods::moon_jump_step(&cpu, link, false);
    };
    invalid_transition(false);
    invalid_transition(true);
    mods::moon_jump_reset();
    assert(!mods::moon_jump_move(link, x, z));
    mods::moon_jump_input(pad);
    mods::moon_jump_camera(camera); // valid same-scene capture before initial execute
    mods::moon_jump_step(&cpu, link, true);
    assert(mods::moon_jump_move(link, x, z) && (x != 0 || z != 0));
    mods::moon_jump_step(&cpu, link, false);
    mods::set_infinite(mods::kMoonJump, false);
    mods::moon_jump_step(&cpu, 0x18000000, true);
    // Story grants are off by default, require consent, and fail closed without a complete backup.
    std::memset(mem::ptr(base),0xA5,0x2000);
    st16(player,14);st16(player+2,14);st16(player+4,158);st8(player+0x12,0);
    std::memcpy(mem::ptr(player+0x30),"sea\0\0\0\0\0",8);
    std::memcpy(mem::ptr(0x104741E4),"Atorizk",8);
    st8(player+0xB4,1);st8(player+0xBF,0x80);
    std::array<uint8_t,0x2000> before;
    std::memcpy(before.data(),mem::ptr(base),before.size());
    assert(mods::story_item_confirmation_needed());
    mods::request_story_item(mods::kStoryDin);mods::cheats_service();
    assert(mods::story_item_confirmation_needed() && last_notice.find("story's order")!=std::string::npos);
    assert(!std::memcmp(before.data(),mem::ptr(base),before.size()));
    namespace fs=std::filesystem;
    const fs::path temp=fs::temp_directory_path()/("wwhd-story-cheats-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(temp/"save/user");
    auto& backups=mods::saves::store();backups.configure(temp/"save",temp/"backups");backups.log=[](const std::string&){};
    mods::request_story_item(mods::kStoryDin,true);mods::cheats_service();
    assert(!std::memcmp(before.data(),mem::ptr(base),before.size())); // an empty save is not a backup
    assert(last_notice.find("Save in the game first")!=std::string::npos);
    {std::ofstream f(temp/"save/user/photo.sav");f<<"only a photo";}
    mods::request_story_item(mods::kStoryDin);mods::cheats_service();
    assert(!std::memcmp(before.data(),mem::ptr(base),before.size())); // photos alone are not a Quest Log backup
    fs::remove(temp/"save/user/photo.sav");
    {std::ofstream f(temp/"save/user/cking.sav");f<<"before story cheats";}
    backups.free_space=[](const fs::path&){return uint64_t(0);};
    mods::request_story_item(mods::kStoryDin);mods::cheats_service();
    assert(!std::memcmp(before.data(),mem::ptr(base),before.size()));
    assert(last_notice.find("not granted")!=std::string::npos);
    backups.free_space=[](const fs::path&){return UINT64_MAX;};
    backups.copy_file=[&](const fs::path& from,const fs::path& to){
        assert(!std::memcmp(before.data(),mem::ptr(base),before.size()));
        fs::copy_file(from,to);
    };
    mods::request_story_item(mods::kStoryDin);mods::cheats_service();
    auto expected=before;expected[0x20+0xBF]|=2;
    assert(!std::memcmp(expected.data(),mem::ptr(base),expected.size()));
    assert(backups.list().size()==1 && !mods::story_item_confirmation_needed());
    const auto backup_file=backups.root()/backups.list()[0].name/"save/user/cking.sav";
    std::string text;
    {std::ifstream backup(backup_file);text.assign(std::istreambuf_iterator<char>(backup),{});} // closed before remove_all (Windows)
    assert(text=="before story cheats");
    for(auto [item,bit]:{std::pair{mods::kStoryNayru,1},std::pair{mods::kStoryFarore,4}}){
        mods::request_story_item(item);mods::cheats_service();expected[0x20+0xBF]|=bit;
        assert(!std::memcmp(expected.data(),mem::ptr(base),expected.size()));
    }
    assert(ld8(player+0xBF)==0x87); // all three pearls; unrelated bits and all placement events preserved
    mods::request_story_item(mods::kStorySword);mods::cheats_service();
    expected[0x20+0xB4]|=8;expected[0x20+0x0E]=0x3E;
    assert(!std::memcmp(expected.data(),mem::ptr(base),expected.size()));
    assert(ld8(player+0xB4)==9 && !(ld8(player+0xB4)&4)); // real phase-3 ownership; no new Medli-removal bit
    assert(backups.list().size()==1); // first grant only; never overwrite the original backup
    mods::cheats_service();assert(!std::memcmp(expected.data(),mem::ptr(base),expected.size()));
    // A title-screen request is refused, rather than silently applying to the next Quest Log.
    std::memcpy(mem::ptr(0x104741E4),"Name\0\0\0\0",8);
    mods::request_story_item(mods::kStoryDin);mods::cheats_service();
    assert(last_notice.find("Load a Quest Log")!=std::string::npos);
    std::memcpy(mem::ptr(0x104741E4),"Atorizk",8);
    mods::cheats_service();assert(!std::memcmp(expected.data(),mem::ptr(base),expected.size()));
    fs::remove_all(temp);

}
