// Explicit headless diagnostics; no actions are selected in ordinary UI runs.
#pragma once
#include "../mods/packages.h"
#include "../mods/catalogue_io.h"
namespace overlay::diagnostic {
struct SetupPlan {
    std::string id,error;
    mods::json::Value sources;
};
inline SetupPlan read_setup_plan(bool no_host,const char* manager,const char* id,const char* input) {
    SetupPlan plan;
    // Do not even read a diagnostic input unless every isolation switch is set.
    if(!no_host||!manager||!*manager||!id||!*id||!input||!*input)return plan;
    plan.id=id;
    try {
        mods::catalogue::require(mods::catalogue::identifier(plan.id),"Invalid diagnostic mod ID");
        mods::catalogue::require(std::filesystem::path(input).is_absolute(),"Diagnostic input must be absolute");
        plan.sources=mods::json::parse(mods::catalogue::read_bounded(input,65536));
        mods::catalogue::require(plan.sources.type==mods::json::Value::Object&&plan.sources.object.size()<=16,"Invalid diagnostic sources");
        for(const auto& [game,path]:plan.sources.object)
            mods::catalogue::require(mods::catalogue::identifier(game)&&path.type==mods::json::Value::String&&
                !path.text.empty()&&path.text.size()<=4096,"Invalid diagnostic source entry");
    }catch(const std::exception&){plan.error="Invalid headless setup input";}
    return plan;
}
inline size_t next_required_step(const std::vector<mods::packages::SetupView>& steps) {
    for(size_t i=0;i<steps.size();++i)if(!steps[i].step.optional&&!steps[i].satisfied)return i;
    return steps.size();
}
inline bool setup_ready(const std::vector<mods::packages::SetupView>& steps) {
    return next_required_step(steps)==steps.size();
}
} // namespace overlay::diagnostic
