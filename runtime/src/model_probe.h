// SPDX-License-Identifier: MPL-2.0
#pragma once
namespace model_probe {
// Private observation of the compiled model wrapper's actual persistent probe
// state. This grants no native invocation, ownership or resource authority.
bool native_refresh_quiescent() noexcept;
}
