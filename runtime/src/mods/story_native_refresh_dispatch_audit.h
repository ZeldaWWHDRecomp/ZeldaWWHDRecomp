// SPDX-License-Identifier: MPL-2.0
#pragma once
#include "ppc.h"
#include <functional>
namespace story_native_refresh_dispatch::detail {
// Data-only audit inputs/results; they cannot construct a Guard.
struct Route {PpcFunc canonical{},current{};bool flagged=false,chain=false;};
struct View {bool hooks_sealed=false,text_sealed=false,probe_quiet=false,registration_exact=false;std::function<bool(uint32_t,Route&)>route;};
bool audit(const View&)noexcept;
}
