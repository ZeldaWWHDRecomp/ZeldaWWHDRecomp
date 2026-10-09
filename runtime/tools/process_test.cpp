#include "platform/process.h"
#include <cassert>
#include <filesystem>
#include <iostream>
int main(int argc,char** argv) {
    if(argc>1&&std::string(argv[1])=="--directory") {std::cout<<std::filesystem::current_path().string();return 0;}
    if(argc>1&&std::string(argv[1])=="--flood") {std::cout<<std::string(2*1024*1024,'x')<<"final diagnostic";return 1;}
    if(argc>1&&std::string(argv[1])=="--child") {
        for(int i=2;i<argc;++i)std::cout<<argv[i]<<'\n';
        return 7;
    }
    std::vector<std::string> arguments={std::filesystem::absolute(argv[0]).string(),"--child","space here","\"quotes\"","trailing\\","$()`;&%PATH%",""};
    auto result=host::run_process(arguments);
    assert(result.error.empty()&&result.code==7);
    std::erase(result.output,'\r'); // Windows CRT text output uses CRLF
    assert(result.output=="space here\n\"quotes\"\ntrailing\\\n$()`;&%PATH%\n\n");
    assert(host::run_process({"wwhd-nonexistent-tool-721993"}).code==-1);
    assert(host::run_process({}).code==-1);
    auto cwd=std::filesystem::temp_directory_path();
    auto directory=host::run_process({std::filesystem::absolute(argv[0]).string(),"--directory"},cwd.string());
#ifdef __ANDROID__
    assert(!directory.error.empty());
#else
    assert(directory.error.empty()&&directory.code==0&&std::filesystem::equivalent(directory.output,cwd));
#endif
    auto flood=host::run_process({std::filesystem::absolute(argv[0]).string(),"--flood"});
    assert(flood.code==1&&flood.output.size()==1024*1024&&flood.output.ends_with("final diagnostic"));
    std::cout<<"tool argument forwarding passed\n";
}
