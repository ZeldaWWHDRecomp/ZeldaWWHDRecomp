// SPDX-License-Identifier: MPL-2.0
// Based on VR-WakerHD's draw batching (@mkooner), contributed under MPL-2.0.
// From VR-WakerHD (https://github.com/VR-WakerHD/VR-WakerHD) runtime/src/gfx/vulkan/batch_glsl.cpp at commit e06fb7b99cbb
// (ZeldaWWHDRecomp discussion #535). Copyright (C) 2026 VR-WakerHD (https://github.com/VR-WakerHD/).
// Changes for ZeldaWWHDRecomp: none.
// A vertex shader's batched variant: batch_glsl.h.
#include "batch_glsl.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace gfxvk::vk::batch {
namespace {

bool ident_char(char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; }

// the first occurrence of word (a whole identifier) at or after from; npos if none
size_t find_word(const std::string& s, const std::string& word, size_t from = 0) {
    for (size_t at = s.find(word, from); at != std::string::npos; at = s.find(word, at + 1)) {
        const bool before = at == 0 || !ident_char(s[at - 1]);
        const bool after = at + word.size() >= s.size() || !ident_char(s[at + word.size()]);
        if (before && after) return at;
    }
    return std::string::npos;
}

bool fail(std::string* why, const std::string& what) {
    if (why) *why = what;
    return false;
}

// the declared size of `vec4 uf_blockVS<i>[n];` (0 if not laid out so)
uint32_t block_vec4s(const std::string& s, size_t member, const std::string& var) {
    const size_t open = member + var.size();
    if (open >= s.size() || s[open] != '[') return 0;
    const size_t close = s.find(']', open);
    if (close == std::string::npos) return 0;
    const std::string n = s.substr(open + 1, close - open - 1);
    if (n.empty() || !std::all_of(n.begin(), n.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)); }))
        return 0;
    return uint32_t(std::strtoul(n.c_str(), nullptr, 10));
}

// a guest uniform buffer's block: `UNIFORM_BUFFER_LAYOUT(...) uniform uniformBlockVS<i> { vec4 uf_blockVS<i>[n]; };`
bool rewrite_block(std::string& s, uint32_t i, uint32_t pieces, uint32_t vec4s, std::string* why) {
    const std::string block = "uniformBlockVS" + std::to_string(i);
    const std::string var = "uf_blockVS" + std::to_string(i);
    const size_t decl = find_word(s, block);
    if (decl == std::string::npos) return fail(why, "no declaration of " + block);
    const size_t member = find_word(s, var, decl);
    const size_t close = s.find("};", decl);
    if (member == std::string::npos || close == std::string::npos || member > close)
        return fail(why, "unexpected layout of " + block);
    const size_t type = s.rfind("vec4", member);
    const size_t memberEnd = s.find(';', member);
    if (type == std::string::npos || type < decl || memberEnd == std::string::npos || memberEnd > close)
        return fail(why, "unexpected member of " + block);
    // the member: the run's pieces, each the block as the shader declares it
    const std::string piece = var + "_piece";
    s.replace(type, memberEnd - type, piece + " " + var + "_in[" + std::to_string(pieces) + "]");
    const size_t closeNow = s.find("};", type);
    s.insert(closeNow + 2, "\n#define " + var + " " + var + "_in[gl_InstanceIndex].v\n");
    // its type, before the block's line
    size_t line = s.rfind('\n', decl);
    line = line == std::string::npos ? 0 : line + 1;
    s.insert(line, "struct " + piece + " { vec4 v[" + std::to_string(vec4s) + "]; };\n");
    return true;
}

// the support block: `layout(...) uniform ufBlock { uniform <type> <name>[...]; ... };`, its members referred to by
// name in the code
bool rewrite_support_block(std::string& s, uint32_t pieces, std::string* why) {
    const size_t decl = find_word(s, "ufBlock");
    if (decl == std::string::npos) return fail(why, "no support block");
    const size_t open = s.find('{', decl);
    const size_t close = open == std::string::npos ? std::string::npos : s.find("};", open);
    if (close == std::string::npos) return fail(why, "unexpected layout of the support block");
    const std::string body = s.substr(open + 1, close - open - 1);
    std::string members, defines;
    size_t pos = 0;
    while (pos < body.size()) {
        const size_t end = body.find(';', pos);
        if (end == std::string::npos) {
            if (body.find_first_not_of(" \t\r\n", pos) != std::string::npos) return fail(why, "a support block member without ;");
            break;
        }
        std::string member = body.substr(pos, end - pos);
        pos = end + 1;
        // (a struct member has no `uniform` qualifier)
        const size_t qualifier = find_word(member, "uniform");
        if (qualifier != std::string::npos) member.erase(qualifier, 7);
        size_t limit = member.find('[');
        if (limit == std::string::npos) limit = member.size();
        size_t nameEnd = limit;
        while (nameEnd > 0 && !ident_char(member[nameEnd - 1])) --nameEnd;
        size_t nameStart = nameEnd;
        while (nameStart > 0 && ident_char(member[nameStart - 1])) --nameStart;
        if (nameStart == nameEnd) return fail(why, "a support block member without a name");
        const std::string name = member.substr(nameStart, nameEnd - nameStart);
        members += member + ";";
        defines += "#define " + name + " ufBlock_in[gl_InstanceIndex]." + name + "\n";
    }
    if (defines.empty()) return fail(why, "an empty support block");
    s.replace(open + 1, close - open + 1, "\nufBlock_piece ufBlock_in[" + std::to_string(pieces) + "];\n};\n" + defines);
    size_t line = s.rfind('\n', decl);
    line = line == std::string::npos ? 0 : line + 1;
    s.insert(line, "struct ufBlock_piece {" + members + "\n};\n");
    return true;
}

}  // namespace

bool rewrite(const std::string& glsl, Variant& v, uint32_t maxRange, std::string& out, std::string* why) {
    out = glsl;
    // the guest's instance ID: 0 in every draw of the run
    const std::string instanceDefine = "#define gl_InstanceID gl_InstanceIndex";
    const size_t define = out.find(instanceDefine);
    if (define != std::string::npos) out.replace(define, instanceDefine.size(), "#define gl_InstanceID 0");
    else if (find_word(out, "gl_InstanceID") != std::string::npos || find_word(out, "gl_InstanceIndex") != std::string::npos)
        return fail(why, "the instance ID used without the translator's define");
    // every batched block's declared size, and the pieces they all fit
    uint32_t largest = 0;
    for (uint32_t i = 0; i < 16; ++i) {
        v.vec4s[i] = 0;
        if (!(v.blocks >> i & 1)) continue;
        const std::string block = "uniformBlockVS" + std::to_string(i);
        const std::string var = "uf_blockVS" + std::to_string(i);
        const size_t decl = find_word(out, block);
        const size_t member = decl == std::string::npos ? decl : find_word(out, var, decl);
        if (member == std::string::npos) return fail(why, "no declaration of " + block);
        v.vec4s[i] = block_vec4s(out, member, var);
        if (!v.vec4s[i]) return fail(why, "no array size for " + block);
        largest = std::max(largest, v.vec4s[i]);
    }
    uint32_t largestBytes = largest * 16;
    if (v.support) {
        if (!v.supportBytes || v.supportBytes % 16) return fail(why, "the support block's size");
        largestBytes = std::max(largestBytes, v.supportBytes);
    }
    v.pieces = largestBytes ? std::min<uint32_t>(kMaxPieces, maxRange / largestBytes) : kMaxPieces;
    if (v.pieces < 2) return fail(why, "a batched block too large for two pieces");
    for (uint32_t i = 0; i < 16; ++i)
        if ((v.blocks >> i & 1) && !rewrite_block(out, i, v.pieces, v.vec4s[i], why)) return false;
    if (v.support && !rewrite_support_block(out, v.pieces, why)) return false;
    return true;
}

}  // namespace gfxvk::vk::batch
