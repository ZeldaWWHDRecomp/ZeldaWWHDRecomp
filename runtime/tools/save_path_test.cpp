#include "save_path.h"
#include <cassert>
#include <initializer_list>
int main() {
    using namespace ss;
    for(bool android:{false,true}) {
        assert(use_streaming_save(SavePath::Auto,android)==android);
        assert(use_streaming_save(SavePath::Streaming,android));
        assert(!use_streaming_save(SavePath::Fast,android));
    }
    assert(parse_save_path("auto")==SavePath::Auto);
    assert(parse_save_path("streaming")==SavePath::Streaming);
    assert(parse_save_path("fast")==SavePath::Fast);
    for(auto value:{"", "unknown", "STREAMING", "1"})assert(parse_save_path(value)==SavePath::Auto);
    for(auto mode:{SavePath::Auto,SavePath::Streaming,SavePath::Fast})assert(parse_save_path(save_path_name(mode))==mode);
#ifdef __ANDROID__
    assert(use_streaming_save(SavePath::Auto));
#else
    assert(!use_streaming_save(SavePath::Auto));
#endif
}
