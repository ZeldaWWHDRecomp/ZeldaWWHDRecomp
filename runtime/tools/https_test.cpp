#include "platform/https.h"
#include "mods/catalogue_io.h"
#include <cassert>
#include <chrono>
#include <iostream>
int main(int argc,char** argv) {
    namespace fs=std::filesystem;
    auto root=fs::temp_directory_path()/("wwhd-https-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directory(root);auto file=root/"download";
    try {
        bool rejected=false;
        try{host::download_https("http://example.com",file,1024);}catch(const std::exception&){rejected=true;}
        assert(rejected&&!fs::exists(file));
        // Optional live probe; ordinary CTest never requires network access.
        if(argc==2) {
            host::download_https(argv[1],file,2*1024*1024);
            auto data=mods::catalogue::read_bounded(file,2*1024*1024);assert(!data.empty());
            std::cout<<"Downloaded "<<data.size()<<" bytes over HTTPS\n";
            fs::remove(file);rejected=false;
            try{host::download_https(argv[1],file,1);}catch(const std::exception&){rejected=true;}
            assert(rejected&&!fs::exists(file));
        }
    }catch(...){fs::remove_all(root);throw;}
    fs::remove_all(root);
}
