#pragma once
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
// Local compatibility names used by the HLE filesystem only. Enumeration uses real
// host directories; guest paths and guest handles remain owned by hle/fs.cpp.
#define stat _stat64
#define fstat _fstat64
#define fileno _fileno
#ifndef S_ISDIR
#define S_ISDIR(mode) (((mode)&_S_IFMT)==_S_IFDIR)
#endif
struct dirent { char d_name[4096]; };
struct DIR {
 std::filesystem::directory_iterator cursor,end;
 dirent entry{};
 bool first=true;
};
inline DIR* opendir(const char* path) {
 std::error_code ec;
 auto cursor=std::filesystem::directory_iterator(path,ec);
 if(ec)return nullptr;
 return new DIR{std::move(cursor)};
}
inline dirent* readdir(DIR* directory) {
 std::error_code ec;
 if(directory->cursor==directory->end)return nullptr;
 if(!directory->first)directory->cursor.increment(ec);
 directory->first=false;
 if(ec||directory->cursor==directory->end)return nullptr;
 auto name=directory->cursor->path().filename().string();
 if(name.size()>=sizeof directory->entry.d_name)return nullptr;
 memcpy(directory->entry.d_name,name.c_str(),name.size()+1);
 return &directory->entry;
}
inline int closedir(DIR* directory) {delete directory;return 0;}
#else
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace host {
inline int64_t file_tell(FILE* file) {
#ifdef _WIN32
 return _ftelli64(file);
#else
 return ftello(file);
#endif
}
// Writes a file's buffered data and asks the system to put it on the disk (before a rename makes it
// the real file). False: the data may not be complete.
inline bool flush_file(FILE* file) {
 if(std::fflush(file)!=0)return false;
#ifdef _WIN32
 return _commit(_fileno(file))==0;
#elif defined(__APPLE__)
 // fsync leaves the data in the drive's cache on macOS; F_FULLFSYNC flushes it (some file systems,
 // such as network ones, don't support it: then fsync)
 return fcntl(fileno(file),F_FULLFSYNC)==0||fsync(fileno(file))==0;
#else
 return fsync(fileno(file))==0;
#endif
}
inline int file_seek(FILE* file,uint64_t offset,int origin) {
#ifdef _WIN32
 return _fseeki64(file,(int64_t)offset,origin);
#else
 return fseeko(file,(off_t)offset,origin);
#endif
}
}
