#pragma once
#include <cstddef>
#include <string_view>
namespace crash_context {
using Output = void (*)(int, const char*, size_t);
void initialize(); // before installing handlers
void refresh();    // normal game thread, periodically; never from a handler
void note(int fd, Output out);
void redact(int fd, std::string_view text, Output out); // bounded, no allocation or locks
}
