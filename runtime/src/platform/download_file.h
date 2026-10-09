// A streaming download owns a new file and removes partial data on every failure.
#pragma once
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace host {
class DownloadFile {
    std::filesystem::path path_;
    FILE* file_=nullptr;
    uint64_t limit_,written_=0;
    bool complete_=false;
public:
    DownloadFile(const std::filesystem::path& path,uint64_t limit):path_(path),limit_(limit) {
        if(!limit)throw std::runtime_error("Invalid download size limit");
#ifdef _WIN32
        int fd=_wopen(path.c_str(),_O_WRONLY|_O_CREAT|_O_EXCL|_O_BINARY,_S_IREAD|_S_IWRITE);
#else
        int fd=open(path.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC,0600);
#endif
        if(fd<0)throw std::runtime_error("Cannot create download file");
#ifdef _WIN32
        file_=_fdopen(fd,"wb");
        if(!file_)_close(fd);
#else
        file_=fdopen(fd,"wb");
        if(!file_)close(fd);
#endif
        if(!file_){std::error_code error;std::filesystem::remove(path_,error);throw std::runtime_error("Cannot open download file");}
    }
    DownloadFile(const DownloadFile&)=delete;
    DownloadFile& operator=(const DownloadFile&)=delete;
    ~DownloadFile(){if(file_)std::fclose(file_);if(!complete_){std::error_code error;std::filesystem::remove(path_,error);}}
    void append(const void* data,size_t bytes) {
        if(!file_||complete_)throw std::runtime_error("Download file is closed");
        if(bytes>limit_-written_)throw std::runtime_error("Download exceeds size limit");
        if(bytes&&std::fwrite(data,1,bytes,file_)!=bytes)throw std::runtime_error("Cannot write download file");
        written_+=bytes;
    }
    void finish() {
        if(!file_||complete_)throw std::runtime_error("Download file is closed");
        auto file=file_;file_=nullptr;
        if(std::fclose(file)!=0)throw std::runtime_error("Cannot finish download file");
        complete_=true;
    }
    uint64_t size()const{return written_;}
};
} // namespace host
