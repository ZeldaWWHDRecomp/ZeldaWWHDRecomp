// Production slot I/O with synthetic memory only. No game/assets are read.
#include "../src/savestate.cpp"
#include "state_file_section.h"
#include <cassert>
#include <fstream>
namespace config {std::string game_dir="/nonexistent-synthetic-state-test";}
void log_msg(const char*,...) {}
namespace mem {std::vector<AllocRec> runtime_alloc_log(){return {{kRuntimeStart,32,0}};}}
namespace dispatch {std::vector<std::pair<uint32_t,std::string>> host_functions(){return {{0xc2000000,"synthetic"}};}}
namespace {
std::vector<uint8_t> file_bytes(const std::string& path) {
    std::ifstream in(path,std::ios::binary);return {std::istreambuf_iterator<char>(in),{}};
}
void replace_bytes(const std::string& path,const std::vector<uint8_t>& data) {
    std::ofstream out(path,std::ios::binary|std::ios::trunc);out.write(reinterpret_cast<const char*>(data.data()),data.size());assert(out.good());
}
ss::FullStateHeader header(uint32_t version=3) {
    ss::FullStateHeader h{};memcpy(h.magic,"WWHDSTAT",8);h.version=version;h.header_size=sizeof h;
    h.cpu_size=sizeof(Cpu);h.game_id=ss::game_id();return h;
}
void legacy_fixture(uint32_t version,bool oldHeader,const std::vector<uint8_t>& raw) {
    auto h=header(version);h.header_size=oldHeader?ss::kLegacyHeaderSize:sizeof h;h.raw_size=raw.size();h.blocks=1;
    std::vector<uint8_t> encoded(raw.size()*2+1024);size_t n=0;
#ifdef __APPLE__
    if(version==1) {
        std::vector<uint8_t> scratch(compression_encode_scratch_buffer_size(COMPRESSION_LZ4));
        n=compression_encode_buffer(encoded.data(),encoded.size(),raw.data(),raw.size(),scratch.data(),COMPRESSION_LZ4);
    } else
#endif
    n=LZ4_compress_default(reinterpret_cast<const char*>(raw.data()),reinterpret_cast<char*>(encoded.data()),raw.size(),encoded.size());
    assert(n && n<raw.size());
    FILE* f=fopen(ss::slot_path(1).c_str(),"wb");assert(f);assert(fwrite(&h,h.header_size,1,f)==1);
    uint32_t record[2]={uint32_t(raw.size()),uint32_t(n)};assert(fwrite(record,sizeof record,1,f)==1);assert(fwrite(encoded.data(),1,n,f)==n);fclose(f);
    std::string why;auto loaded=ss::read_slot(1,why);assert(loaded && loaded->payload==raw);
}
}
int main() {
    const auto directory=std::filesystem::temp_directory_path()/std::filesystem::path("wwhd-synthetic-state-test-"+std::to_string(getpid()));
    assert(!std::filesystem::exists(directory));std::filesystem::create_directory(directory);
    setenv("WWHD_STATE_DIR",directory.string().c_str(),1);
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::filesystem::remove_all(path);}} cleanup{directory};
    constexpr uint32_t base=0x10000000;
    std::vector<uint8_t> memory(4*ss::kMemoryChunk), restored(memory.size(),0xcc);
    for(size_t i=0;i<ss::kMemoryChunk;++i)memory[i]=uint8_t(1+i%31);
    uint32_t random=0xa12345b;
    for(size_t i=2*ss::kMemoryChunk;i<3*ss::kMemoryChunk;++i){random^=random<<13;random^=random>>17;random^=random<<5;memory[i]=uint8_t(random);}
    auto pointer=[&](uint32_t a){return memory.data()+a-base;};
    auto touched=[](const uint8_t*,size_t){return true;};
    const std::vector<ss::Region> regions={{base,uint32_t(memory.size())}};
    ss::Writer metadata,files;files.u32(1);files.u32(0);files.u32(0);ss::put_section(metadata,ss::kSecFiles,files);
    const auto plan=ss::plan_regions(regions,pointer,touched);
    assert(plan.pages[0]==std::vector<uint32_t>({0,2}));
    ss::Writer old;ss::capture_regions(old,regions,pointer,touched);
    struct Sink {ss::Writer value;bool bytes(const void* p,size_t n){value.bytes(p,n);return true;}} sink;
    assert(ss::stream_regions(sink,plan,pointer));assert(sink.value.b==old.b && plan.size==old.b.size());
    uint64_t rawSize=0;assert(ss::write_memory_slot(1,header(),metadata,regions,pointer,touched,rawSize));
    std::string why;auto state=ss::read_slot(1,why);assert(state && state->h.version==3 && state->payload.size()==rawSize);
    assert(ss::check_file_section(state->section(ss::kSecFiles),why));
    ss::restore_regions(state->regs,state->chunks,[&](uint32_t a){return restored.data()+a-base;},touched);assert(restored==memory);

    // Writer selection changes saving only: either selected path loads both formats.
    ss::Writer fastPayload;
    fastPayload.bytes(metadata.b.data(),metadata.b.size());
    fastPayload.u32(ss::kSecMemory);fastPayload.u64(old.b.size());
    fastPayload.bytes(old.b.data(),old.b.size());
    assert(fastPayload.b==state->payload);
    auto fastHeader=header(ss::kFastVersion);
    assert(ss::write_slot(2,fastHeader,fastPayload.b));
    const auto fastBytes=file_bytes(ss::slot_path(2));
    for(auto mode:{ss::SavePath::Auto,ss::SavePath::Streaming,ss::SavePath::Fast}) {
        ss::set_save_path(mode);assert(ss::save_path()==mode);
        ss::g_save_path_setting=-1;assert(ss::save_path()==mode);
        auto loadedFast=ss::read_slot(2,why);
        auto loadedStream=ss::read_slot(1,why);
        assert(loadedFast && loadedStream && loadedFast->payload==loadedStream->payload);
        assert(loadedFast->h.version==ss::kFastVersion && loadedStream->h.version==3);
        assert(file_bytes(ss::slot_path(2))==fastBytes);
    }
    {std::ofstream config(directory/"full_save_path.cfg");config<<"unknown-value\n";}
    ss::g_save_path_setting=-1;assert(ss::save_path()==ss::SavePath::Auto);
    const auto good=file_bytes(ss::slot_path(1));
    // Independent golden raw-LZ4 block for ABCD repeated16 times, not produced by our writer.
    const uint8_t golden[]={0x4f,'A','B','C','D',4,0,36,0x50,'D','A','B','C','D'};
    for(bool legacyHeader:{false,true}) {
        FILE* f=tmpfile();assert(f);auto h=header(2);h.header_size=legacyHeader?ss::kLegacyHeaderSize:sizeof h;h.raw_size=64;h.blocks=1;
        fwrite(&h,h.header_size,1,f);uint32_t block[2]={64,sizeof golden};fwrite(block,sizeof block,1,f);fwrite(golden,1,sizeof golden,f);rewind(f);
        ss::FullStateHeader parsed{};assert(ss::read_full_state_header(f,parsed,why));std::vector<uint8_t> decoded;
        assert(ss::read_full_state_payload(f,parsed,decoded,why));for(size_t i=0;i<64;++i)assert(decoded[i]=="ABCD"[i%4]);fclose(f);
        legacy_fixture(2,legacyHeader,state->payload);
#ifdef __APPLE__
        legacy_fixture(1,legacyHeader,state->payload);
#endif
    }
    // Independent Apple-framing golden: second block refers to the previous
    // decoded block's dictionary, then a stored block and exact terminator.
    const uint8_t appleGolden[]={
        'b','v','4','1',64,0,0,0,sizeof golden,0,0,0,
        0x4f,'A','B','C','D',4,0,36,0x50,'D','A','B','C','D',
        'b','v','4','1',64,0,0,0,10,0,0,0,
        0x0f,4,0,40,0x50,'D','A','B','C','D',
        'b','v','4','-',4,0,0,0,'W','X','Y','Z','b','v','4','$'};
    std::vector<uint8_t> appleRaw(132);
    assert(ss::decode_apple_lz4(appleRaw.data(),appleRaw.size(),appleGolden,sizeof appleGolden));
    for(size_t i=0;i<128;++i)assert(appleRaw[i]=="ABCD"[i%4]);
    assert(!memcmp(appleRaw.data()+128,"WXYZ",4));
    for(bool oldHeader:{false,true}) {
        FILE* file=tmpfile();assert(file);auto h=header(1);
        h.header_size=oldHeader?ss::kLegacyHeaderSize:sizeof h;h.raw_size=appleRaw.size();h.blocks=1;
        fwrite(&h,h.header_size,1,file);uint32_t record[2]={uint32_t(appleRaw.size()),sizeof appleGolden};
        fwrite(record,sizeof record,1,file);fwrite(appleGolden,1,sizeof appleGolden,file);rewind(file);
        ss::FullStateHeader parsed{};assert(ss::read_full_state_header(file,parsed,why));std::vector<uint8_t> decoded;
        assert(ss::read_full_state_payload(file,parsed,decoded,why) && decoded==appleRaw);fclose(file);
    }
    for(size_t length=0;length<sizeof appleGolden;++length)
        assert(!ss::decode_apple_lz4(appleRaw.data(),appleRaw.size(),appleGolden,length));
    auto badApple=std::vector<uint8_t>(appleGolden,appleGolden+sizeof appleGolden);
    badApple[4]=255;assert(!ss::decode_apple_lz4(appleRaw.data(),appleRaw.size(),badApple.data(),badApple.size()));
    badApple.assign(appleGolden,appleGolden+sizeof appleGolden);badApple.push_back(0);
    assert(!ss::decode_apple_lz4(appleRaw.data(),appleRaw.size(),badApple.data(),badApple.size()));
    // Unsupported new format cleanly fails the exact version policy used by old builds.
    assert(state->h.version!=1 && state->h.version!=2);
    // Every rejected input leaves guest bytes untouched; restore is never reached.
    auto reject=[&](std::vector<uint8_t> bytes) {
        replace_bytes(ss::slot_path(1),bytes);std::fill(restored.begin(),restored.end(),0xcc);
        auto failed=ss::read_slot(1,why);assert(!failed);
        assert(std::all_of(restored.begin(),restored.end(),[](uint8_t b){return b==0xcc;}));
    };
    for(size_t length:{size_t(0),size_t(95),size_t(103),sizeof(ss::FullStateHeader)+size_t(7),good.size()-1})reject({good.begin(),good.begin()+length});
    for(size_t at:{offsetof(ss::FullStateHeader,version),offsetof(ss::FullStateHeader,blocks),offsetof(ss::FullStateHeader,raw_size),sizeof(ss::FullStateHeader),good.size()-1}) {
        auto broken=good;broken[at]^=0x80;reject(std::move(broken));
    }
    auto trailing=good;trailing.push_back(0);reject(std::move(trailing));
    // A checksum-free frame is invalid v3 even if zstd itself would decode it.
    auto noChecksum=good;noChecksum[sizeof(ss::FullStateHeader)+8+4]&=~4;reject(std::move(noChecksum));
    // Malformed block counts/sizes fail before a large advertised payload allocation.
    auto huge=good;auto h=header();h.raw_size=ss::kMaxStateBytes;h.blocks=h.raw_size/ss::kStreamBlock;memcpy(huge.data(),&h,sizeof h);reject(std::move(huge));
    // Re-encode structurally invalid logical sections with a valid codec checksum;
    // rejection must come from production Snapshot parsing, before guest restore.
    auto rejectPayload=[&](const std::vector<uint8_t>& payload) {
        FILE* file=fopen(ss::slot_path(1).c_str(),"wb");assert(file);
        ss::FullStateStreamWriter writer(file,header());assert(writer.bytes(payload.data(),payload.size())&&writer.finish());fclose(file);
        assert(!ss::read_slot(1,why));
    };
    auto duplicate=state->payload;duplicate.insert(duplicate.end(),metadata.b.begin(),metadata.b.end());rejectPayload(duplicate);
    auto duplicatePage=state->payload;
    const size_t memAt=state->sections.at(ss::kSecMemory).first;
    uint32_t zero=0;memcpy(duplicatePage.data()+memAt+4+12+4+ss::kMemoryChunk,&zero,4);rejectPayload(duplicatePage);
    auto outside=state->payload;const uint32_t unmapped=0x90000000;memcpy(outside.data()+memAt+4,&unmapped,4);rejectPayload(outside);
    auto missingFile=state->payload;missingFile.erase(missingFile.begin(),missingFile.begin()+metadata.b.size());rejectPayload(missingFile);
    ss::Writer emptyFileMetadata;ss::put_section(emptyFileMetadata,ss::kSecFiles,ss::Writer{});
    auto emptyFile=emptyFileMetadata.b;emptyFile.insert(emptyFile.end(),missingFile.begin(),missingFile.end());rejectPayload(emptyFile);
    replace_bytes(ss::slot_path(1),good);
    std::filesystem::create_directory(ss::slot_path(1,"tmp"));
    assert(!ss::write_memory_slot(1,header(),metadata,regions,pointer,touched,rawSize));assert(file_bytes(ss::slot_path(1))==good);
    // FILE preflight is nonmutating and rejects every truncated schema before any restore.
    for(size_t n=0;n<files.b.size();++n)assert(!ss::check_file_section(ss::Reader(files.b.data(),n),why));
    ss::Writer shortFiles;shortFiles.u32(4);shortFiles.u32(3);
    for(uint32_t i=1;i<=3;++i){shortFiles.u32(i);shortFiles.str("a");shortFiles.str("r");shortFiles.u64(0);}shortFiles.u32(0);
    assert(ss::check_file_section(ss::Reader(shortFiles.b.data(),shortFiles.b.size()),why));
    shortFiles.b.push_back(0);assert(!ss::check_file_section(ss::Reader(shortFiles.b.data(),shortFiles.b.size()),why));
    ss::Writer allocations;allocations.u32(mem::kRuntimeStart+128);allocations.u32(2);
    allocations.pod(mem::AllocRec{mem::kRuntimeStart,32,0});allocations.pod(mem::AllocRec{mem::kRuntimeStart+32,16,0});
    assert(ss::check_allocs(ss::Reader(allocations.b.data(),allocations.b.size()),why));
    for(size_t n=0;n<allocations.b.size();++n)assert(!ss::check_allocs(ss::Reader(allocations.b.data(),n),why));
    auto invalidLate=allocations;uint32_t badAddress=mem::kHostStart;
    memcpy(invalidLate.b.data()+8+sizeof(mem::AllocRec),&badAddress,4);
    assert(!ss::check_allocs(ss::Reader(invalidLate.b.data(),invalidLate.b.size()),why));
    allocations.b.push_back(0);assert(!ss::check_allocs(ss::Reader(allocations.b.data(),allocations.b.size()),why));
    ss::Writer dispatch;dispatch.u32(1);dispatch.u32(0xc2000000);dispatch.str("synthetic");
    assert(ss::check_dispatch(ss::Reader(dispatch.b.data(),dispatch.b.size()),why));
    for(size_t n=0;n<dispatch.b.size();++n)assert(!ss::check_dispatch(ss::Reader(dispatch.b.data(),n),why));
    dispatch.b.push_back(0);assert(!ss::check_dispatch(ss::Reader(dispatch.b.data(),dispatch.b.size()),why));
    puts("production streaming, v1/v2 compatibility, checksum/size/truncation, file preflight and slot preservation pass");
}
