#include "platform/process.h"
#include <cassert>
#include <filesystem>
#include <iostream>
int main(int argc,char** argv) {
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
    std::cout<<"tool argument forwarding passed\n";
}
