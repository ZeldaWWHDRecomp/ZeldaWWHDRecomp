#include "mods/guest_files.h"
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
int main(int argc,char** argv) {
    namespace fs=std::filesystem;
    fs::path work=argc>1?argv[1]:"build/sdk2-tests/guest-files-work";
    assert(!fs::exists(work));fs::create_directories(work);
    guestmods::Files a(work/"Data"/"first"),b(work/"Data"/"second");
    const char data[]="hello";char out[32]{};
    assert(a.write("progress.bin",data,5)==5);
    assert(a.read("progress.bin",out,sizeof out)==5&&std::string(out,5)=="hello");
    assert(b.read("progress.bin",out,sizeof out)==-1);
    assert(a.write("progress.bin",data,2)==2);
    assert(a.read("progress.bin",out,sizeof out)==2); // overwrite truncates
    assert(a.write("empty.bin",nullptr,0)==0);
    for(const auto* name:{"../outside","nested/file","/absolute","bad\\path","NUL","CON.txt","COM1","LPT0","trailing.","hidden:stream",".hidden"})
        assert(a.write(name,data,5)==-1);
    assert(a.write("large.bin",data,1024*1024+1)==-1);
    fs::path outside=fs::absolute(work/"outside.bin");std::ofstream(outside)<<"original";
    std::error_code error;fs::create_symlink(outside,work/"Data/first/link.bin",error);
    if(!error){assert(a.read("link.bin",out,sizeof out)==-1);assert(a.write("link.bin",data,5)==-1);}
    fs::create_hard_link(outside,work/"Data/first/hard.bin",error);
    if(!error)assert(a.write("hard.bin",data,5)==-1);
    std::ifstream original(outside);std::string text;original>>text;assert(text=="original");original.close();
    fs::remove_all(work);std::cout<<"per-mod file isolation/path restrictions passed\n";
}
