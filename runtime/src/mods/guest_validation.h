// Validate a translated module completely before publishing memory, dispatch entries or hooks.
#pragma once
#include "wwhd_guest_abi.h"
#include <cstdio>
#include <string>
#include <unordered_set>
#include <unordered_map>

namespace guestmods {
// existing_replacement returns the owning mod's name, or an empty string.
template<class IsGameFunction, class ExistingReplacement>
bool validate_module(const WWHDGuestModuleV1& m, const std::string& owner,
                     IsGameFunction is_game_function, ExistingReplacement existing_replacement,
                     std::string& error) {
    constexpr uint32_t start = 0x7F000000, end = 0x80000000;
    if (m.mem_base < start || m.mem_base >= end || !m.mem_size ||
        m.mem_size > end - m.mem_base || m.image_size > m.mem_size) {
        error = "module memory outside the guest mod region"; return false;
    }
    if ((m.image_size && !m.image) || (m.func_count && !m.funcs) ||
        (m.hook_count && !m.hooks) || !m.translator) {
        error = "module has missing tables"; return false;
    }
    std::unordered_map<uint32_t, PpcFunc> functions;
    std::unordered_set<uint32_t> replacements;
    for (uint32_t i = 0; i < m.func_count; ++i) {
        const auto& f = m.funcs[i];
        if (!f.fn || (f.addr & 3) || f.addr < m.mem_base ||
            f.addr - m.mem_base >= m.mem_size || !functions.emplace(f.addr, f.fn).second) {
            error = "module has an invalid or duplicate function"; return false;
        }
    }
    for (uint32_t i = 0; i < m.hook_count; ++i) {
        const auto& h = m.hooks[i];
        if (h.kind < WWHD_GUEST_REPLACE || h.kind > WWHD_GUEST_HOOK_RETURN ||
            !h.fn || !functions.count(h.func)) {
            error = "module has an invalid hook descriptor"; return false;
        }
        if (functions.at(h.func) != h.fn) {
            error = "hook function does not match its dispatch entry"; return false;
        }
        if (!is_game_function(h.target)) {
            char b[96]; std::snprintf(b, sizeof b, "hook target %08X is not a game function", h.target);
            error = b; return false;
        }
        if (h.kind == WWHD_GUEST_REPLACE) {
            std::string prior = existing_replacement(h.target);
            if (!replacements.insert(h.target).second) prior = owner;
            if (!prior.empty()) {
                char b[16]; std::snprintf(b, sizeof b, "%08X", h.target);
                error = "replacement conflict at " + std::string(b) + ": " + prior + " and " + owner;
                return false;
            }
        }
    }
    return true;
}
} // namespace guestmods
