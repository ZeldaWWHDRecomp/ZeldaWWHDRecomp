// Real inline storage/serialization with an injected deterministic clock symbol.
// No Vulkan, game, worker thread, device or production clock implementation.
#include "gfx/vulkan/fallback_stats.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
namespace fs = std::filesystem;
static uint64_t nowValue=100, clockCalls=0;
namespace gfxvk::handoff_trace {
uint64_t native_now_ns() { ++clockCalls; return nowValue; }
}
using namespace gfxvk;
namespace stats = gfxvk::fallback_stats;
static void env(const std::string& value) {
#ifdef _WIN32
    assert(_putenv_s("WWHD_VK_FALLBACK_STATS",value.c_str())==0);
#else
    assert(setenv("WWHD_VK_FALLBACK_STATS",value.c_str(),1)==0);
#endif
}
static std::string contents(const fs::path& p) {
    std::ifstream in(p,std::ios::binary);assert(in.good());
    return {std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};
}
int main() {
    const auto dir=fs::temp_directory_path()/ ("wwhd-fallback-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    assert(fs::create_directory(dir));
    struct Cleanup {fs::path p;~Cleanup(){std::error_code ec;fs::remove_all(p,ec);}} cleanup{dir};
    auto reset=[&](const char* name) {
        stats::storage.reset();stats::initialized=false;clockCalls=0;nowValue=100;
        auto path=dir/name;env(path.string());stats::initialize();assert(stats::storage);return path;
    };
    // OFF cannot allocate diagnostic storage, query a clock or create output.
    env("");stats::initialize();assert(stats::initialized && !stats::storage);
    stats::begin_frame(0);stats::complete_frame(0);stats::native_prefix_publication();
    {stats::Drain d(stats::Boundary::EncoderFallback,1,PodFailureReason::CommandLimit,false);d.complete();}
    assert(stats::flush() && clockCalls==0 && fs::is_empty(dir));
    auto path=reset("zero.json");
    for(uint64_t i=0;i<3;++i){stats::begin_frame(i);stats::complete_frame(i);}
    assert(stats::storage->frameCount==3 && !stats::storage->invalid);
    for(size_t i=0;i<3;++i){const auto& f=stats::storage->frames[i];assert(f.id==i && f.completed && f.attempts==0 && f.completedDrains==0);}
    assert(stats::flush());auto first=contents(path);assert(first.find("\"schema\":2")!=std::string::npos);
    stats::begin_frame(3);stats::native_prefix_publication();assert(stats::flush() && contents(path)==first);
    reset("gap.json");stats::complete_frame(0);stats::begin_frame(2);assert(stats::storage->invalid);
    reset("incomplete-frame.json");stats::begin_frame(0);stats::begin_frame(1);assert(stats::storage->invalid);
    path=reset("completed.json");stats::begin_frame(0);
    stats::native_prefix_publication();stats::native_prefix_publication();
    assert(stats::storage->frames[0].nativePrefixPublications==2 && stats::storage->frames[0].attempts==0 && stats::storage->frames[0].completedDrains==0 && clockCalls==0);
    {stats::Drain d(stats::Boundary::EncoderFallback,4,PodFailureReason::ByteBudgetOrOverflow,true);nowValue=350;d.complete();}
    auto& f=stats::storage->frames[0];assert(f.attempts==1 && f.completedDrains==1 && f.emptyPrefixes==1 && f.nativePrefixPublications==2);
    assert(f.rejected[4][size_t(PodFailureReason::ByteBudgetOrOverflow)]==1);
    const auto& interval=stats::storage->intervals[0];assert(interval.begin==100 && interval.end==350 && interval.completed && clockCalls==2);
    stats::complete_frame(0);assert(stats::flush() && contents(path).find("\"native_prefix_publications\":2")!=std::string::npos);
    reset("throw.json");stats::begin_frame(0);
    try {stats::Drain d(stats::Boundary::EncoderFallback,2,PodFailureReason::AllocationFailure,false);nowValue=200;throw std::runtime_error("synthetic raw drain failure");}catch(const std::runtime_error&){}
    assert(stats::storage->invalid && stats::storage->frames[0].completedDrains==0 && !stats::storage->intervals[0].completed && !stats::flush());
    reset("nested.json");stats::begin_frame(0);
    {stats::Drain outer(stats::Boundary::EncoderFallback,1,PodFailureReason::CommandLimit,false);{stats::Drain inner(stats::Boundary::EncoderFallback,2,PodFailureReason::CommandLimit,false);inner.complete();}nowValue=200;outer.complete();}
    assert(stats::storage->invalid && stats::storage->intervalCount==1);
    reset("zero-clock.json");stats::begin_frame(0);nowValue=0;
    {stats::Drain d(stats::Boundary::EncoderFallback,1,PodFailureReason::InvalidInput,false);d.complete();}assert(stats::storage->invalid);
    reset("reverse-clock.json");stats::begin_frame(0);
    {stats::Drain d(stats::Boundary::EncoderFallback,1,PodFailureReason::InvalidInput,false);nowValue=99;d.complete();}assert(stats::storage->invalid);
    path=reset("frame-cap.json");for(size_t i=0;i<stats::MaxFrames;++i)stats::complete_frame(i);
    assert(!stats::storage->invalid && stats::storage->frameCount==stats::MaxFrames);stats::begin_frame(stats::MaxFrames);
    assert(stats::storage->invalid && stats::storage->frameCount==stats::MaxFrames && !stats::flush());assert(fs::file_size(path)<32*1024*1024);
    path=reset("interval-cap.json");stats::begin_frame(0);
    for(size_t i=0;i<stats::MaxIntervals;++i){stats::Drain d(stats::Boundary::EncoderFallback,1,PodFailureReason::CommandLimit,false);nowValue+=1;d.complete();}
    assert(!stats::storage->invalid && stats::storage->intervalCount==stats::MaxIntervals);
    {stats::Drain d(stats::Boundary::EncoderFallback,1,PodFailureReason::CommandLimit,false);d.complete();}
    assert(stats::storage->invalid && stats::storage->intervalCount==stats::MaxIntervals && !stats::flush());assert(fs::file_size(path)<32*1024*1024);
    reset("counter-cap.json");stats::begin_frame(0);stats::storage->frames[0].nativePrefixPublications=std::numeric_limits<uint32_t>::max();stats::native_prefix_publication();assert(stats::storage->invalid);
    reset("fatal-retry.json");stats::begin_frame(0);stats::fatal_submit_retry(PodFailureReason::CommandLimit);
    assert(stats::storage->invalid && stats::storage->frames[0].attempts==0 && stats::storage->frames[0].fatalSubmitRetryReasons[size_t(PodFailureReason::CommandLimit)]==1);
    stats::storage.reset();env("");return 0;
}
