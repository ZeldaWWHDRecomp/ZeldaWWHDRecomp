// Flat, per-mod data files. Never resolve a caller-controlled directory or follow a file symlink.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cerrno>
#include <filesystem>
#include <mutex>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace guestmods {
class Files {
    std::filesystem::path root_;
    std::mutex mutex_;
    static bool name_ok(const std::string& name) {
        if(name.empty()||name.size()>128||name[0]=='.'||name.back()=='.')return false;
        for(unsigned char c:name)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
        std::string stem=name.substr(0,name.find('.'));
        std::transform(stem.begin(),stem.end(),stem.begin(),[](unsigned char c){return char(c>='a'&&c<='z'?c-'a'+'A':c);});
        return stem!="CON"&&stem!="PRN"&&stem!="AUX"&&stem!="NUL"&&
               !(stem.size()==4&&(stem.starts_with("COM")||stem.starts_with("LPT"))&&stem[3]>='0'&&stem[3]<='9');
    }
    static bool link(const std::filesystem::path& p) {
#ifdef _WIN32
        auto attributes=GetFileAttributesW(p.c_str());
        return attributes!=INVALID_FILE_ATTRIBUTES&&(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
#else
        return std::filesystem::is_symlink(p);
#endif
    }
    bool directory() {
        // The parent is the manager's Data directory; its parent is trusted manager storage.
        if(link(root_.parent_path())||link(root_))return false;
        std::filesystem::create_directories(root_);
        return !link(root_.parent_path())&&!link(root_)&&std::filesystem::is_directory(root_);
    }
    int32_t transfer(const std::string& name,void* buffer,uint32_t size,bool write) {
        std::lock_guard lock(mutex_);
        if(!name_ok(name)||size>1024*1024||(!buffer&&size))return -1;
        try {if(!directory())return -1;}catch(const std::exception&){return -1;}
#ifdef _WIN32
        auto path=root_/name;
        HANDLE f=CreateFileW(path.c_str(),write?GENERIC_WRITE:GENERIC_READ,FILE_SHARE_READ,nullptr,
                            write?OPEN_ALWAYS:OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(f==INVALID_HANDLE_VALUE)return -1;
        BY_HANDLE_FILE_INFORMATION info{};
        bool ok=GetFileInformationByHandle(f,&info)&&!(info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))&&info.nNumberOfLinks==1;
        if(write&&ok)ok=SetEndOfFile(f)!=0;
        DWORD done=0;
        if(ok)ok=(write?WriteFile(f,buffer,size,&done,nullptr):ReadFile(f,buffer,size,&done,nullptr))!=0;
        CloseHandle(f);return ok?int32_t(done):-1;
#else
        int directory=open(root_.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
        if(directory<0)return -1;
        int f=openat(directory,name.c_str(),(write?O_WRONLY|O_CREAT:O_RDONLY)|O_NOFOLLOW|O_CLOEXEC|O_NONBLOCK,0600);
        close(directory);if(f<0)return -1;
        struct stat status{};bool ok=!fstat(f,&status)&&S_ISREG(status.st_mode)&&status.st_nlink==1;
        if(write&&ok)ok=!ftruncate(f,0);
        uint32_t done=0;
        while(ok&&done<size) {
            ssize_t n=write?::write(f,static_cast<char*>(buffer)+done,size-done) : ::read(f,static_cast<char*>(buffer)+done,size-done);
            if(n<0&&errno==EINTR)continue;
            if(n<0){ok=false;break;}if(!n)break;done+=uint32_t(n);
        }
        close(f);return ok?int32_t(done):-1;
#endif
    }
public:
    explicit Files(std::filesystem::path root):root_(std::move(root)) {}
    int32_t read(const std::string& name,void* buffer,uint32_t size) { return transfer(name,buffer,size,false); }
    int32_t write(const std::string& name,const void* buffer,uint32_t size) { return transfer(name,const_cast<void*>(buffer),size,true); }
};
} // namespace guestmods
