// SPDX-License-Identifier: MPL-2.0
#include "../src/mods/story_native_setinfo_diagnostic.h"
#include "runtime.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <cstdint>

namespace {
std::vector<std::string> logs;
unsigned live_calls=0;
story_native_setinfo_observation::Result inspect(
    const Cpu*,const story_native_stage_owner_hooks::Args& args,uint32_t result,
    story_native_setinfo_candidate::Candidate& candidate) {
    assert(result==1);
    using R=story_native_setinfo_observation::Result;
    if(args.r3_r6[1]==7){
        candidate.info=0x22000000;
        candidate.request_heap=0x23000000;
        candidate.data_heap=0x24000000;
        candidate.archive_holder=0x25000000;
        candidate.archive_resource=0x26000000;
        candidate.archive_base=0x28000000;
        candidate.outer_base=0x28000040;
        candidate.resource=0x28000100;
    }
    return args.r3_r6[1]==7?R::Valid:args.r3_r6[1]==8?R::Manager:R::Candidate;
}
bool live(uint32_t control,const story_native_setinfo_candidate::Candidate& expected,
          story_native_setinfo_candidate::Candidate& out){
    ++live_calls;out=expected;
    return control==0x1234&&expected.info==0x22000000;
}
bool deletion(const story_native_stage_owner_hooks::Args& args,uint32_t control,
              const story_native_setinfo_candidate::Candidate& expected){
    return args.r3_r6[0]==control&&args.r3_r6[1]==7&&expected.info==0x22000000;
}
}
void log_msg(const char* format,...) {
    char line[256];va_list ap;va_start(ap,format);
    std::vsnprintf(line,sizeof(line),format,ap);va_end(ap);logs.emplace_back(line);
}
int main() {
    namespace H=story_native_stage_owner_hooks;
    namespace D=story_native_setinfo_diagnostic;
    assert(!D::enable(nullptr,live,deletion));
    assert(D::enable(inspect,live,deletion));
    assert(!D::enable(inspect,live,deletion));
    Cpu cpu{};cpu.r[3]=0x1234;
    for(uint32_t arg:{7u,8u,9u}){
        cpu.r[3]=0x1234;cpu.r[4]=arg;
        H::invoke_dispatch(&cpu,H::SetInfo(),[&]{
            H::invoke_original(&cpu,H::SetInfo(),[&]{cpu.r[3]=1;});
        });
        assert(cpu.r[3]==1);
    }
    auto c=D::counts();
    assert(c.setinfo==3&&c.setinfo_entered==3&&c.valid==1&&c.manager_invalid==1&&
           c.candidate_invalid==1&&c.failed==0&&c.teardown==0);
    assert(logs.size()==3&&logs[0].find("result=valid")!=std::string::npos&&
           logs[1].find("result=manager")!=std::string::npos);
    H::invoke_original(&cpu,H::SetInfo(),[&]{cpu.r[3]=0;});
    cpu.r[3]=0x1234;cpu.r[4]=7;
    H::invoke_original(&cpu,H::DeleteInfo(),[&]{cpu.r[3]=99;});
    cpu.r[3]=0x23000000;
    H::invoke_original(&cpu,H::StageHeapDtor(),[&]{cpu.r[3]=99;});
    cpu.r[3]=0x25000000;
    H::invoke_original(&cpu,H::ArchiveHolderDtor(),[&]{cpu.r[3]=99;});
    cpu.r[3]=0x26000000;
    H::invoke_original(&cpu,H::SarcResourceDtor(),[&]{cpu.r[3]=99;});
    cpu.r[3]=0x27000000;
    H::invoke_original(&cpu,H::ArchiveResourceDtor(),[&]{cpu.r[3]=99;});
    for(uint32_t p:{0x28000000u,0x28000040u,0x28000100u,0x29000000u}){
        cpu.r[3]=p;
        H::invoke_original(&cpu,H::ArrayDelete(),[&]{assert(cpu.r[3]==p);cpu.r[3]=99;});
    }
    c=D::counts();
    assert(c.failed==1&&c.teardown==5&&c.setinfo==3&&
           c.setinfo_entered==4&&cpu.r[3]==99);
    assert(c.exact_teardown_matches==4&&c.live_at_teardown==4&&live_calls==7);
    assert(c.raw_frees==4&&c.exact_raw_free_matches==3&&c.live_at_raw_free==3);
    assert(logs.size()==10&&logs[3].find("teardown=delete_info")!=std::string::npos&&
           logs[4].find("teardown=request_heap")!=std::string::npos&&
           logs[5].find("teardown=holder")!=std::string::npos&&
           logs[6].find("teardown=sarc_resource")!=std::string::npos&&
           logs[7].find("raw_free=archive_base")!=std::string::npos&&
           logs[8].find("raw_free=outer_base")!=std::string::npos&&
           logs[9].find("raw_free=event_list")!=std::string::npos);
}
