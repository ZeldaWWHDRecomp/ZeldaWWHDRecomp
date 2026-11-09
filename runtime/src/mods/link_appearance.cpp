// SPDX-License-Identifier: MPL-2.0
// Link's appearance: the pure parts (link_appearance.h).
#include "link_appearance.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "link_masks.h"
#include "story_block.h"
#include "story_mail_book.h"

namespace link_appearance {

namespace {
struct SlotInfo { const char* id; const char* label; };
constexpr SlotInfo kSlotInfo[kSlots] = {
    {"hair", "Hair"},           {"skin", "Skin"},   {"eyes", "Eyes"},   {"tunic", "Tunic and hat"},
    {"undershirt", "Undershirt"}, {"pants", "Trousers"}, {"boots", "Boots"}, {"buckle", "Belt buckle"},
};
}  // namespace

const char* slot_id(Slot s) { return unsigned(s) < unsigned(kSlots) ? kSlotInfo[size_t(s)].id : "?"; }
const char* slot_label(Slot s) { return unsigned(s) < unsigned(kSlots) ? kSlotInfo[size_t(s)].label : "?"; }
bool slot_of(const std::string& id, Slot& out) {
    for (int i = 0; i < kSlots; i++)
        if (id == kSlotInfo[size_t(i)].id) return out = Slot(i), true;
    return false;
}

bool Look::operator==(const Look& o) const {
    if (set != o.set) return false;
    for (int i = 0; i < kSlots; i++)
        if (has(Slot(i)) && rgb[size_t(i)] != o.rgb[size_t(i)]) return false;
    return true;
}

std::vector<uint8_t> encode(const Look& l) {
    std::vector<uint8_t> out = {kRecordVersion, uint8_t(l.set >> 8), uint8_t(l.set)};
    for (int i = 0; i < kSlots; i++)
        if (l.has(Slot(i))) {
            const uint32_t c = l.colour(Slot(i));
            out.push_back(uint8_t(c >> 16)), out.push_back(uint8_t(c >> 8)), out.push_back(uint8_t(c));
        }
    return out;
}

bool decode(const uint8_t* data, size_t size, Look& out) {
    out = Look{};
    if (!data || size < 3 || data[0] != kRecordVersion) return false;
    const uint16_t set = uint16_t(data[1] << 8 | data[2]);
    if (set >> kSlots) return false;  // a slot this port doesn't know
    size_t at = 3;
    Look l;
    for (int i = 0; i < kSlots; i++) {
        if (!(set >> i & 1)) continue;
        if (at + 3 > size) return false;
        l.put(Slot(i), uint32_t(data[at]) << 16 | uint32_t(data[at + 1]) << 8 | data[at + 2]);
        at += 3;
    }
    if (at != size) return false;
    out = l;
    return true;
}

std::string to_text(const Look& l) {
    std::string s;
    for (int i = 0; i < kSlots; i++) {
        if (!l.has(Slot(i))) continue;
        char c[24];
        snprintf(c, sizeof c, "%s=%06X", slot_id(Slot(i)), unsigned(l.colour(Slot(i))));
        if (!s.empty()) s += ',';
        s += c;
    }
    return s;
}

bool from_text(const std::string& text, Look& out) {
    Look l;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find(',', at);
        if (end == std::string::npos) end = text.size();
        const std::string item = text.substr(at, end - at);
        at = end + 1;
        if (item.empty()) continue;
        const size_t eq = item.find('=');
        Slot s;
        if (eq == std::string::npos || !slot_of(item.substr(0, eq), s)) return false;
        const std::string hex = item.substr(eq + 1);
        if (hex.size() != 6 || hex.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) return false;
        l.put(s, uint32_t(strtoul(hex.c_str(), nullptr, 16)));
    }
    out = l;
    return true;
}

const std::vector<Preset>& presets(Slot s) {
    // index 0: the game's own colour. The colours are our own picks; each moves the part's painted shading with
    // it (link_masks::tint), so a colour reads as the part's main shade in daylight.
    static const std::vector<Preset> kTable[kSlots] = {
        {{"Original", 0}, {"Platinum", 0xF2EBC8}, {"Copper", 0xD0652E}, {"Auburn", 0x9A3B22}, {"Brown", 0x6E4626},
         {"Black", 0x2B2A30}, {"Silver", 0xC4C8D0}, {"Pink", 0xF08DB8}, {"Blue", 0x4F7FD6}, {"Green", 0x5DB04F}},
        {{"Original", 0}, {"Fair", 0xFCE6D2}, {"Tan", 0xE2B07A}, {"Bronze", 0xB97C4A}, {"Brown", 0x8C5A35},
         {"Deep brown", 0x5E3C26}, {"Pale blue", 0xA8CDEB}, {"Pale green", 0xA9D08E}},
        {{"Original", 0}, {"Blue", 0x2F6FD8}, {"Green", 0x3FA34D}, {"Brown", 0x6B3E1E}, {"Grey", 0x7A8590},
         {"Red", 0xC0282D}, {"Violet", 0x7B3FB0}, {"Gold", 0xC8961E}},
        {{"Original", 0}, {"Red", 0xE52B36}, {"Blue", 0x698DB9}, {"Purple", 0x9A4F9F}, {"Black", 0x34343C},
         {"White", 0xE8E8E2}, {"Yellow", 0xF2C230}, {"Orange", 0xF07A2A}, {"Pink", 0xEE82B4}, {"Teal", 0x2FA59A}},
        {{"Original", 0}, {"White", 0xF4F4EE}, {"Cream", 0xF2E3B8}, {"Sky blue", 0x9CC8EE}, {"Grey", 0x9A9CA2},
         {"Black", 0x35353A}, {"Red", 0xD8444A}, {"Yellow", 0xF2D45A}},
        {{"Original", 0}, {"Cream", 0xF2E3B8}, {"Grey", 0x9A9CA2}, {"Black", 0x35353A}, {"Brown", 0x7A5232},
         {"Blue", 0x5672B0}, {"Green", 0x5E9A52}},
        {{"Original", 0}, {"Black", 0x2E2A28}, {"Dark brown", 0x5A3418}, {"Grey", 0x7C7E84}, {"Red", 0xA8282C},
         {"Blue", 0x34508C}, {"Green", 0x3C6E32}, {"White", 0xE6E2D8}},
        {{"Original", 0}, {"Silver", 0xC8CCD4}, {"Bronze", 0xB07A3C}, {"Copper", 0xC0663A}, {"Black", 0x3A3A40}},
    };
    static const std::vector<Preset> kNone = {{"Original", 0}};
    return unsigned(s) < unsigned(kSlots) ? kTable[size_t(s)] : kNone;
}

int preset_index(const Look& l, Slot s) {
    if (!l.has(s)) return 0;
    const auto& p = presets(s);
    for (size_t i = 1; i < p.size(); i++)
        if (p[i].rgb == l.colour(s)) return int(i);
    return -1;
}

std::vector<uint8_t> parts_of(Slot s) {
    using link_masks::Part;
    switch (s) {
    case Slot::kHair: return {uint8_t(Part::kHair)};
    case Slot::kSkin: return {uint8_t(Part::kSkin)};
    case Slot::kEyes: return {uint8_t(Part::kEyes)};
    case Slot::kTunic: return {uint8_t(Part::kTunic)};
    case Slot::kUndershirt: return {uint8_t(Part::kUndershirt)};
    case Slot::kPants: return {uint8_t(Part::kPants)};
    case Slot::kBoots: return {uint8_t(Part::kBoots)};
    case Slot::kBuckle: return {uint8_t(Part::kBuckle)};
    case Slot::kCount: break;
    }
    return {};
}

// The record in a save file's area: key i, value byte i of encode() (key 0x100: the record's length).
namespace {
constexpr uint16_t kLengthKey = 0x100;
}
Look read_area(const uint8_t* area) {
    story_mail::Book b(const_cast<uint8_t*>(area), story_block::kAreaRecords);
    if (!area || !b.usable() || !b.has(story_mail::kRecordAppearance, kLengthKey)) return {};
    const size_t n = b.get(story_mail::kRecordAppearance, kLengthKey);
    if (n < 3 || n > kMaxRecord) return {};
    uint8_t bytes[kMaxRecord];
    for (size_t i = 0; i < n; i++) {
        if (!b.has(story_mail::kRecordAppearance, uint16_t(i))) return {};
        bytes[i] = b.get(story_mail::kRecordAppearance, uint16_t(i));
    }
    Look l;
    return decode(bytes, n, l) ? l : Look{};
}

bool write_area(uint8_t* area, const Look& l) {
    story_mail::Book b(area, story_block::kAreaRecords);
    if (!area || !b.usable()) return false;
    for (uint16_t k = 0; k < kMaxRecord; k++) b.erase(story_mail::kRecordAppearance, k);
    b.erase(story_mail::kRecordAppearance, kLengthKey);
    if (l.empty()) return true;
    const std::vector<uint8_t> bytes = encode(l);
    if (b.free_records() < bytes.size() + 1) return false;
    for (size_t i = 0; i < bytes.size(); i++) b.set(story_mail::kRecordAppearance, uint16_t(i), bytes[i]);
    b.set(story_mail::kRecordAppearance, kLengthKey, uint8_t(bytes.size()));
    return true;
}

}  // namespace link_appearance
