// Story mods: the record a save file keeps of letters from mods and of the port's story items
// (docs/mod-sdk-v2.md, "Letters"). Pure code without game memory, unit-tested in
// runtime/tools/story_mail_test.cpp.
//
// Where the record lives: in the story block appended to cking.sav, one area per Quest Log
// (story_block.h, story_store.h), with up to 255 records. Earlier builds kept a 19-record form in the
// game's reserved save bytes (dSv_reserve_c, 0x50 bytes at save base + 0x744, zero in every vanilla
// save): that form is only read once, to move it into the block (story_block::migrate).
//
// Format version 2 (bytes, no alignment needs):
//   [0] 'L' (0x4C) magic, [1] format version (2), [2..3] zero;
//   then 19 records of 4 bytes: [0] kind, [1..2] key (big-endian, meaning per kind), [3] value.
//   Kind 0 is a free record. Every user of the block owns one kind (kRecord*). A reader keeps
//   records of kinds it doesn't know untouched, so new kinds need no new format version; the
//   version changes only if this layout changes.
// An all-zero block is an empty record. A block that is not zero and has no valid header holds
// data this port did not write: it is never changed. (Version 1, an unreleased development format,
// is treated the same way.)
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace story_mail {

inline constexpr uint32_t kBookOffset = 0x744;  // from the save base (*0x101F84DC)
inline constexpr uint32_t kBookSize = 0x50;
inline constexpr uint8_t kMagic = 0x4C, kVersion = 2;
inline constexpr unsigned kRecords = (kBookSize - 4) / 4;  // 19

// Record kinds. Add new ones here; never reuse a number.
inline constexpr uint8_t kRecordFree = 0;
inline constexpr uint8_t kRecordLetter = 1;  // key: letter_key(mod, letter), value: State
inline constexpr uint8_t kRecordItem = 2;    // key: kItem*, value: 1 owned
inline constexpr uint8_t kRecordMarker = 3;  // mod charts (story_chart.h): key chart_key(mod, chart), value 1 owned, 3 opened in the chart list, 2 finished
inline constexpr uint8_t kRecordQuest = 4;   // notebook quests (story_notebook.h): key quest_key(mod, quest), value 1..254 step, 255 done
inline constexpr uint8_t kRecordTracked = 5; // the notebook's tracked quest: at most one record, key quest_key, value 1
inline constexpr uint8_t kRecordCarried = 6; // Link carries a quest's letter (story_takeover.h): key quest_key, value 1
inline constexpr uint8_t kRecordQuestFlags = 7; // a quest's 8 flags (wwhd_quest_set_flag): key quest_key, value bit n-1 = flag n
inline constexpr uint8_t kRecordSailLine = 8;   // the King of Red Lions said a step's sail_line (story_sail.h): key quest_key, value the step
inline constexpr uint8_t kRecordQuestOrder = 9; // when a quest was started: key quest_key, value 1.. in start order (255: later)
// 10..15: mod items (story_items_core.h)
inline constexpr uint8_t kRecordLetterEnclosures = 16; // a manifest letter on its own was read and gave what it encloses: key letter_key, value 1
inline constexpr uint8_t kRecordActorFlag = 17; // SDK v3 actors: mod-owned opened/read flags, key actor flag_key, value 0/1

// 18..19: owned prop/enemy markers (story_prop_rewards.h).
inline constexpr uint8_t kRecordQuestActionReward = 20; // pending safe rupee item number for quest_done

inline constexpr uint16_t kItemNotebook = 1;  // the story notebook

// Letter states. The values follow the game's own letter registers (dLetter: 2 waiting in the
// postbox, 3 read); 0 is "never sent".
enum State : uint8_t { kNone = 0, kWaiting = 2, kRead = 3 };

// FNV-1a over "mod id" NUL "letter id", folded to 16 bits; 0 is never used.
inline uint16_t letter_key(const std::string& mod, const std::string& letter) {
    uint32_t h = 2166136261u;
    auto mix = [&h](unsigned char b) { h = (h ^ b) * 16777619u; };
    for (unsigned char b : mod) mix(b);
    mix(0);
    for (unsigned char b : letter) mix(b);
    uint16_t k = uint16_t((h >> 16) ^ (h & 0xFFFF));
    return k ? k : 1;
}

// A view of a record block (host bytes; guest memory is byte-addressed the same way): the 0x50-byte
// form above (19 records), or a larger area with the same layout and more records (story_block.h).
class Book {
public:
    explicit Book(uint8_t* bytes, unsigned records = kRecords) : b_(bytes), n_(records) {}
    unsigned records() const { return n_; }
    bool empty() const {
        if (!b_) return true;
        for (uint32_t i = 0; i < 4 + 4 * n_; i++)
            if (b_[i]) return false;
        return true;
    }
    // ours: a valid header, or still all zero (a fresh save)
    bool usable() const { return b_ && (empty() || (b_[0] == kMagic && b_[1] == kVersion && !b_[2] && !b_[3])); }  // none: no save
    // The value of a record, 0 when there is none.
    uint8_t get(uint8_t kind, uint16_t key) const {
        int i = usable() ? find(kind, key) : -1;
        return i < 0 ? 0 : b_[4 + 4 * i + 3];
    }
    bool has(uint8_t kind, uint16_t key) const { return usable() && find(kind, key) >= 0; }
    // Sets a record's value, adding the record if needed. False when the block is not ours or full.
    bool set(uint8_t kind, uint16_t key, uint8_t value) {
        if (!usable() || kind == kRecordFree) return false;
        int i = find(kind, key);
        for (unsigned r = 0; r < n_ && i < 0; r++)
            if (record_kind(r) == kRecordFree) i = int(r);
        if (i < 0) return false;
        b_[0] = kMagic, b_[1] = kVersion, b_[2] = 0, b_[3] = 0;
        uint8_t* p = b_ + 4 + 4 * i;
        p[0] = kind, p[1] = uint8_t(key >> 8), p[2] = uint8_t(key), p[3] = value;
        return true;
    }
    // Frees a record. False when the block is not ours or has no such record.
    bool erase(uint8_t kind, uint16_t key) {
        int i = usable() ? find(kind, key) : -1;
        if (i < 0) return false;
        uint8_t* p = b_ + 4 + 4 * i;
        p[0] = p[1] = p[2] = p[3] = 0;
        return true;
    }
    unsigned free_records() const {
        if (!usable()) return 0;
        unsigned n = 0;
        for (unsigned r = 0; r < n_; r++) n += record_kind(r) == kRecordFree;
        return n;
    }
    uint8_t record_kind(unsigned r) const { return b_[0] == kMagic ? b_[4 + 4 * r] : kRecordFree; }
    uint16_t record_key(unsigned r) const { return uint16_t(b_[4 + 4 * r + 1] << 8 | b_[4 + 4 * r + 2]); }
    uint8_t record_value(unsigned r) const { return b_[4 + 4 * r + 3]; }

    // letters
    State state(uint16_t key) const { return State(get(kRecordLetter, key)); }
    bool set_state(uint16_t key, State s) { return set(kRecordLetter, key, s); }

private:
    int find(uint8_t kind, uint16_t key) const {
        if (b_[0] != kMagic || kind == kRecordFree) return -1;
        for (unsigned r = 0; r < n_; r++)
            if (record_kind(r) == kind && record_key(r) == key) return int(r);
        return -1;
    }
    uint8_t* b_;
    unsigned n_;
};

inline constexpr size_t kMaxIdBytes = 32, kMaxSenderBytes = 48, kMaxTextBytes = 1200;
inline constexpr size_t kMaxUnits = 700;  // UTF-16 code units of the composed letter, NUL excluded

// Letter IDs: 1..32 of a-z 0-9 - _ .
inline bool valid_id(const std::string& id) {
    if (id.empty() || id.size() > kMaxIdBytes) return false;
    for (char ch : id)
        if (!((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.')) return false;
    return true;
}

// UTF-8 -> UTF-16, appended to out. False on invalid UTF-8 or a control character other than a
// line break (the game's message text uses control codes for its own tags, so a mod can't pass
// any: a letter is plain text). Tabs and CR are not allowed either.
inline bool append_utf16(const std::string& in, bool allow_newline, std::u16string& out) {
    for (size_t i = 0; i < in.size();) {
        unsigned char c0 = (unsigned char)in[i];
        uint32_t cp, n;
        if (c0 < 0x80) cp = c0, n = 1;
        else if ((c0 & 0xE0) == 0xC0) cp = c0 & 0x1F, n = 2;
        else if ((c0 & 0xF0) == 0xE0) cp = c0 & 0x0F, n = 3;
        else if ((c0 & 0xF8) == 0xF0) cp = c0 & 0x07, n = 4;
        else return false;
        if (i + n > in.size()) return false;
        for (uint32_t k = 1; k < n; k++) {
            unsigned char cx = (unsigned char)in[i + k];
            if ((cx & 0xC0) != 0x80) return false;
            cp = cp << 6 | (cx & 0x3F);
        }
        static constexpr uint32_t kMin[5] = {0, 0, 0x80, 0x800, 0x10000};
        if (cp < kMin[n] || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        if (cp == '\n') {
            if (!allow_newline) return false;
        } else if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0) || cp == 0xFEFF) {
            return false;
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(char16_t(0xD800 + (cp >> 10)));
            out.push_back(char16_t(0xDC00 + (cp & 0x3FF)));
        } else {
            out.push_back(char16_t(cp));
        }
        i += n;
    }
    return true;
}

// The letter as the message window shows it: the text, a blank line and the sender as the
// signature. Empty when the input is not valid or too long.
inline std::u16string compose(const std::string& sender, const std::string& text) {
    std::u16string out;
    if (sender.empty() || sender.size() > kMaxSenderBytes || text.empty() || text.size() > kMaxTextBytes) return {};
    if (!append_utf16(text, true, out)) return {};
    while (!out.empty() && out.back() == u'\n') out.pop_back();
    if (out.empty()) return {};
    out += u"\n\n";
    if (!append_utf16(sender, false, out)) return {};
    if (out.size() > kMaxUnits) return {};
    return out;
}

}  // namespace story_mail
