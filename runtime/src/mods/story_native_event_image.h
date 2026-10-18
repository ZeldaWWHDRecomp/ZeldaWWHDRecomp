// SPDX-License-Identifier: MPL-2.0
#pragma once
#include <cstdint>
#include <string>
#include <vector>
namespace story_native_event_image {
// Structural native WAIT fixture only; publication/leases and movement are separate.
std::vector<uint8_t> make_wait_fixture(uint32_t frames = 30);
bool validate_wait_fixture(const std::vector<uint8_t>& image, std::string& why);
}
