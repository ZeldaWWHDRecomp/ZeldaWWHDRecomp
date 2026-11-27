// native_draw/j3d_desc_lists.h: does a J3D material description's program display list still hold the bytes its define
// decoded? (native_draw/j3d_desc.cpp wwhd_j3ddesc_begin; header-only so tools/j3d_desc_lists_test.cpp can test it)
//
// The description key (decomp J3DNativeDesc.h DescKey) names the shader's display list by address and size only
// (kKeyList, kKeyListBytes), and the define keeps the list's bytes (Desc::lists): the item's register effect, the
// vertex, pixel and fetch program addresses among them, is decoded from those bytes, not from guest memory. A list
// rewritten at the same address (the shader's refresh rewrites it in place; a room or model loaded where another was,
// with its objects at the same addresses but its programs elsewhere) leaves the key as it was, so the description kept
// the old program addresses: the draw then translated whatever lies there now ("Latte GLSL translation failed",
// issue #682: setup's model sweep in the decomp version, Cave10_Room0 after Cave09). A list that differs from the
// define's bytes defines the description again. WWHD_NATIVE_J3D_DESC_LISTCHECK=0 turns the check off (as before).
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace native_draw::j3ddesc {

// the key words that name the program's display list (J3DNativeDesc.h DescKeyWord)
constexpr uint32_t kKeyListWord = 11, kKeyListBytesWord = 12;

// KEY (WORDS words) as the hook sent it; DEFINED: the list bytes the description was defined with; NOW: the list's
// bytes in guest memory now (nullptr: not readable). True: the description's list still holds (or it has none).
inline bool program_list_holds(const uint32_t* key, uint32_t words, const std::vector<uint8_t>& defined,
                               const uint8_t* now) {
    if (words <= kKeyListBytesWord) return true;
    const uint32_t bytes = key[kKeyListBytesWord];
    if (bytes == 0) return true;  // no list: the stage programs are bound (descBindStagePrograms)
    if (!now || defined.size() != bytes) return false;
    return std::memcmp(defined.data(), now, bytes) == 0;
}

}  // namespace native_draw::j3ddesc
