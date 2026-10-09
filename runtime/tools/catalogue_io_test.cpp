#include "mods/catalogue_io.h"
#include <cassert>
#include <chrono>

template<class F>void rejects(F action) {
    bool failed=false;try{action();}catch(const std::exception&){failed=true;}assert(failed);
}
int main() {
    namespace fs=std::filesystem;using namespace mods::catalogue;
    auto root=fs::temp_directory_path()/("wwhd-catalogue-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root/"packages");
    assert(mods::hash::sha256_text("abc")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    assert(mods::hash::sha256_text("")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    try {
        auto file=root/"packages"/"synthetic.zip";
        {std::ofstream out(file,std::ios::binary);out<<"abc";}
        Download d{"packages/synthetic.zip","ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",3};
        auto resolved=confined_file(root,d.url);verify_package(resolved,d);
        assert(read_bounded(file,3)=="abc");rejects([&]{read_bounded(file,2);});
        auto bad=d;bad.size=4;rejects([&]{verify_package(file,bad);});
        bad=d;bad.sha256[0]='0';rejects([&]{verify_package(file,bad);});
        rejects([&]{confined_file(root,"../outside.zip");});
        rejects([&]{confined_file(root,"packages/missing.zip");});
        assert(!relative_file("tools/CON.exe")&&!relative_file("NUL")&&!relative_file("out/LPT1.txt"));
        std::error_code error;fs::create_directory_symlink(root/"packages",root/"link",error);
        if(!error)rejects([&]{confined_file(root,"link/synthetic.zip");});
        {std::ofstream out(file,std::ios::binary);out<<"abd";}
        rejects([&]{verify_package(file,d);});
    }catch(...){fs::remove_all(root);throw;}
    fs::remove_all(root);
}
