// The game's metadata reader (mods/code_mods_io.h) against setup's real atomic publisher
// (tools/installer/test_code_mods_io.py, issue #282). Synthetic temporary data only.
#include "mods/code_mods_io.h"
#include <atomic>
#include <cassert>
#include <iostream>
#include <string>
#include <thread>
int main(int argc,char** argv) {
    assert(argc==2);
    const auto path=std::filesystem::u8path(argv[1]);
    const auto scratch=path.parent_path()/"reader-validation.json";
    auto rejected=[&] {
        bool failed=false;
        try { mods::code::read_build_json(scratch); }
        catch(const std::exception&) { failed=true; }
        assert(failed);
        assert(!mods::code::poll_build_json(scratch));
    };
    rejected(); // no publication yet
    std::ofstream(scratch)<<"{broken";
    rejected();
    std::ofstream(scratch)<<std::string(mods::code::build_json_limit+1,' ');
    rejected();
    std::filesystem::remove(scratch);
    // Read as fast as possible, independently of the writer, until the parent says "stop".
    std::atomic<bool> stop{false};
    std::thread control([&]{std::string line;while(std::getline(std::cin,line)&&line!="stop"){}stop=true;});
    // The parent's seed publication (done -1) is read before the writer starts.
    // Publication counters stay signed: the seed is -1 (converting -1.0 to an unsigned type is undefined
    // behaviour; x86-64 gives ULONG_MAX, arm64 0).
    unsigned long reads=0;
    long long last=(long long)mods::code::read_build_json(path).get("done").number;
    assert(last==-1);
    std::cout<<"ready"<<std::endl;
    std::string raw;
    while(!stop) {
        // Mostly bare open-read-close cycles, so a handle is open nearly all the time (the worst case
        // for the writer); every 16th read also parses and checks the publication fully.
        for(int i=0;i<15;++i) {
            assert(host::read_shared(path,raw,mods::code::build_json_limit));
            assert(raw.size()>512*1024&&raw.front()=='{'&&raw.back()=='}');
            ++reads;
        }
        auto value=mods::code::read_build_json(path);  // raises (test failure) on any failed read
        assert(value.get("stage").string()=="building");
        assert(value.get("payload").string().size()==512*1024);
        auto polled=mods::code::poll_build_json(path);
        assert(polled&&polled->get("stage").string()=="building");
        const auto done=(long long)value.get("done").number;
        assert(done>=last);  // never an older publication after a newer one
        last=done;++reads;
    }
    control.join();
    std::cout<<reads<<std::endl;
}
