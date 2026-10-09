#pragma once
#include <cstdint>
#include <filesystem>
#include <string>

namespace host {
// Blocking: call from a worker. Certificate validation stays enabled; redirects must use HTTPS.
// Creates a new destination, streams at most limit bytes, removes partial files on failure.
void download_https(const std::string& url,const std::filesystem::path& destination,uint64_t limit);
}
