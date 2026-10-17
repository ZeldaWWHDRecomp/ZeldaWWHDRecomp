// Full-state saving policy. Readers do not depend on the selected writer.
#pragma once
#include <string_view>
namespace ss {
enum class SavePath { Auto, Streaming, Fast };
inline constexpr SavePath parse_save_path(std::string_view value) {
    return value=="streaming" ? SavePath::Streaming : value=="fast" ? SavePath::Fast : SavePath::Auto;
}
inline constexpr const char* save_path_name(SavePath value) {
    return value==SavePath::Streaming ? "streaming" : value==SavePath::Fast ? "fast" : "auto";
}
inline constexpr bool use_streaming_save(SavePath value,bool android) {
    return value==SavePath::Streaming || (value==SavePath::Auto && android);
}
inline constexpr bool use_streaming_save(SavePath value) {
#ifdef __ANDROID__
    return use_streaming_save(value,true);
#else
    return use_streaming_save(value,false);
#endif
}
} // namespace ss
