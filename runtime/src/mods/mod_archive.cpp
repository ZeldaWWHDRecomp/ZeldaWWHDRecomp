#include "mod_archive.h"
#include <fstream>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <vector>
#include <zlib.h>
namespace mods::archive {
namespace fs=std::filesystem;
constexpr uint64_t kFileLimit=128ull*1024*1024,kTotalLimit=512ull*1024*1024;
bool relative_path(const std::string& name) {
    if(name.empty()||name.size()>512||name.find('\\')!=std::string::npos||name.find(':')!=std::string::npos||name.find('\0')!=std::string::npos)return false;
    fs::path p(name);if(p.is_absolute())return false;
    for(const auto& part:p)if(part==".."||part=="."||part.empty())return false;
    return true;
}
static void require(bool ok,const char* error){if(!ok)throw std::runtime_error(error);}
void stage(const fs::path& source,const fs::path& dest) {
    require(!fs::exists(dest),"Staging directory already exists");fs::create_directories(dest);
    uint64_t total=0;size_t entries=0;
    if(fs::is_directory(source)) {
        require(!fs::is_symlink(source),"Mod folders may not be symbolic links");
        for(const auto& entry:fs::recursive_directory_iterator(source)) {
            require(++entries<=4096,"Mod has too many files");
            require(!entry.is_symlink(),"Mod folders may not contain symbolic links");
            auto relative=entry.path().lexically_relative(source);
            require(relative_path(relative.generic_string()),"Invalid mod file path");
            auto output=dest/relative;
            if(entry.is_directory()){fs::create_directories(output);continue;}
            require(entry.is_regular_file(),"Unsupported mod file type");
            auto size=entry.file_size();total+=size;require(size<=kFileLimit&&total<=kTotalLimit,"Mod size limit exceeded");
            fs::create_directories(output.parent_path());fs::copy_file(entry.path(),output);
        }
        return;
    }
    require(fs::is_regular_file(source)&&fs::file_size(source)<=kTotalLimit,"Invalid or oversized mod archive");
    std::ifstream in(source,std::ios::binary);std::vector<unsigned char> b((std::istreambuf_iterator<char>(in)),{});
    auto u16=[&](size_t p)->unsigned{require(p+2<=b.size(),"Truncated ZIP");return b[p]|unsigned(b[p+1])<<8;};
    auto u32=[&](size_t p)->uint32_t{require(p+4<=b.size(),"Truncated ZIP");return u16(p)|(uint32_t(u16(p+2))<<16);};
    require(b.size()>=22,"Not a ZIP mod package");
    size_t end=b.size()-22;bool found=false;
    for(size_t n=0;n<=65535&&n<=end;n++)if(u32(end-n)==0x06054b50&&end-n+22+u16(end-n+20)==b.size()){end-=n;found=true;break;}
    require(found,"ZIP directory not found");require(u16(end+4)==0&&u16(end+6)==0&&u16(end+8)==u16(end+10),"Split ZIP archives are unsupported");
    entries=u16(end+10);require(entries&&entries<=4096,"Invalid ZIP file count");
    size_t pos=u32(end+16),central_size=u32(end+12);require(pos<=end&&central_size==end-pos,"Invalid ZIP directory bounds");
    std::set<std::string> names;
    for(size_t i=0;i<entries;i++) {
        require(u32(pos)==0x02014b50&&pos+46<=end,"Invalid ZIP directory entry");
        unsigned flags=u16(pos+8),method=u16(pos+10),len=u16(pos+28),extra=u16(pos+30),comment=u16(pos+32);
        uint32_t crc=u32(pos+16),packed=u32(pos+20),size=u32(pos+24),attrs=u32(pos+38),local=u32(pos+42);
        require(!(flags&1)&&(method==0||method==8),"Encrypted or unsupported ZIP compression");
        require(pos+46+len+extra+comment<=end,"Truncated ZIP entry name");
        std::string name(reinterpret_cast<const char*>(b.data()+pos+46),len);pos+=46+len+extra+comment;
        bool dir=!name.empty()&&name.back()=='/';if(dir)name.pop_back();
        require(relative_path(name)&&names.insert(name).second,"Invalid or duplicate ZIP path");
        require(((attrs>>16)&0170000)!=0120000,"ZIP symbolic links are unsupported");
        total+=size;require(size<=kFileLimit&&total<=kTotalLimit,"Expanded mod size limit exceeded");
        require(uint64_t(local)+30<=end&&u32(local)==0x04034b50,"Invalid ZIP local header");
        auto local_name_len=u16(local+26),local_extra=u16(local+28);size_t data=uint64_t(local)+30+local_name_len+local_extra;
        require(data<=end&&packed<=end-data,"Truncated ZIP file data");
        require(u16(local+8)==method&&u16(local+6)==flags,"Inconsistent ZIP headers");
        std::string local_name(reinterpret_cast<const char*>(b.data()+local+30),local_name_len);
        require(local_name==(dir?name+"/":name),"Inconsistent ZIP file name");
        if(dir){require(!size,"Directory has ZIP data");fs::create_directories(dest/name);continue;}
        std::vector<unsigned char> out(size);
        if(method==0){require(packed==size,"Invalid stored ZIP size");std::copy_n(b.data()+data,size,out.data());}
        else {
            unsigned char scratch;z_stream z{};z.next_in=b.data()+data;z.avail_in=packed;z.next_out=size?out.data():&scratch;z.avail_out=size?size:1;
            require(inflateInit2(&z,-MAX_WBITS)==Z_OK,"ZIP inflate init failed");int code=inflate(&z,Z_FINISH);inflateEnd(&z);
            require(code==Z_STREAM_END&&z.total_out==size&&z.total_in==packed,"Invalid compressed ZIP payload");
        }
        require(crc32(0,out.data(),size)==crc,"ZIP checksum mismatch");
        auto output=dest/name;fs::create_directories(output.parent_path());std::ofstream file(output,std::ios::binary);file.write(reinterpret_cast<const char*>(out.data()),out.size());require(bool(file),"Cannot write staged mod file");
    }
    require(pos==end,"Unexpected ZIP directory data");
}
}
