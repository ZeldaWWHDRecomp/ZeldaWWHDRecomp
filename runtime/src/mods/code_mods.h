#pragma once
#include <string>
namespace mods::code {
struct Status {
    bool requested=false, target=false, building=false, ready=false;
    unsigned done=0,total=0;
    std::string stage,error,exe;
};
bool enabled();
void startup(int argc, char** argv);
bool restart(std::string& error);
void request(bool on, const std::string& pending_mod = {}); // confirmation is shown in the Mods page
Status status();
void begin();
void cancel();
void dismiss();
}
