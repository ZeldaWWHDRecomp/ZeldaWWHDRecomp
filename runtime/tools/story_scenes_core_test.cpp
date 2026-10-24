#include "mods/story_scenes_core.h"
#include <cassert>
#include <cstdio>
#include <type_traits>
using namespace story_scenes;
struct Fake final:Driver {
    unsigned acquired=0,entered=0,updated=0,finished=0,released=0,complete_at=2;
    bool acquire_ok=true,enter_ok=true,finish_ok=true,update_ok=true;
    StopReason reason=StopReason::Aborted;
    size_t chosen=0;bool choose=false;std::vector<size_t> visits;
    bool acquire()override{++acquired;return acquire_ok;}
    bool enter(size_t n)override{++entered;visits.push_back(n);return enter_ok;}
    size_t successor(size_t n)override{return choose?(n==0?chosen:size_t(3)):n+1;}
    StepResult update(size_t,unsigned elapsed)override{++updated;return !update_ok?StepResult::Failed:elapsed>=complete_at?StepResult::Complete:StepResult::Running;}
    bool finish()override{++finished;return finish_ok;}
    void release(StopReason r)override{++released;reason=r;}
};
int main(){
    static_assert(!std::is_copy_constructible_v<Player>);
    // an area trigger with "again": armed again only once Link is outside the area by the margin
    assert(!area_rearmed(0,120)&&!area_rearmed(120.f*120.f,120)&&!area_rearmed((120+kAreaAgainMargin)*(120+kAreaAgainMargin),120)&&area_rearmed(200.f*200.f,120));
    {Player p;Fake f;assert(!p.start({},f,1,false));assert(!p.start({{1,false}},f,1,false));assert(!p.start({{1,true},{0,true}},f,1,false));assert(!p.start({{0,false},{0,true}},f,1,false));assert(!p.start({{18001,false},{0,true}},f,1,false));assert(!p.start({{2,false},{0,true}},f,1,true));assert(f.acquired==0);}
    {Player p;Fake f;f.acquire_ok=false;assert(!p.start({{2,false},{0,true}},f,1,false));assert(f.acquired==1&&f.released==0);}
    {Player p;Fake f;assert(p.start({{2,false},{0,true}},f,1,false));assert(!p.start({{2,false},{0,true}},f,1,false));assert(f.acquired==1);p.tick(1);assert(p.step()==0&&p.elapsed()==1);p.tick(1);assert(p.step()==1&&p.active());p.tick(1);assert(!p.active()&&f.finished==1&&f.released==1&&f.reason==StopReason::Finished);p.stop();p.tick(1);assert(f.released==1);}
    {Player p;Fake f;f.complete_at=18000;assert(p.start({{18000,false},{0,true}},f,1,false));for(unsigned n=0;n<18000;++n)p.tick(1);assert(p.active());p.tick(1);assert(!p.active()&&f.finished==1&&f.reason==StopReason::Finished);}
    {Player p;Fake f;f.enter_ok=false;assert(!p.start({{2,false},{0,true}},f,1,false));assert(f.released==1&&f.finished==0&&f.reason==StopReason::Failed);}
    {Player p;Fake f;f.update_ok=false;assert(p.start({{2,false},{0,true}},f,1,false));p.tick(1);assert(f.released==1&&f.finished==0&&f.reason==StopReason::Failed);}
    {Player p;Fake f;f.complete_at=3;assert(p.start({{2,false},{0,true}},f,1,false));p.tick(1);p.tick(1);assert(f.released==1&&f.finished==0&&f.reason==StopReason::TimedOut);}
    for(auto reason:{StopReason::Skipped,StopReason::Aborted,StopReason::StateLoaded,StopReason::Interrupted}){
        Player p;Fake f;assert(p.start({{0,true}},f,7,false));
        if(reason==StopReason::Skipped)p.tick(7,false,true);
        else if(reason==StopReason::Aborted)p.stop();
        else if(reason==StopReason::StateLoaded)p.tick(8,false,true);
        else p.tick(7,true,true);
        assert(!p.active()&&f.released==1&&f.finished==0&&f.reason==reason);p.stop();assert(f.released==1);
    }
    {Player p;Fake f;f.finish_ok=false;assert(p.start({{0,true}},f,7,false));p.tick(7);assert(f.finished==1&&f.released==1&&f.reason==StopReason::Failed);}
    {Player p;Fake f;assert(p.start({{2,false},{0,true}},f,1,false));p.tick(1);p.stop();assert(p.start({{2,false},{0,true}},f,2,false));p.tick(2);p.tick(2);p.tick(2);assert(f.released==2&&f.finished==1);}
    const std::vector<Timing> graph={{2,false,{1,2}},{2,false,{3}},{2,false,{3}},{0,true,{}}};
    assert(valid_graph(graph));
    {auto separate=graph;separate[1].frames=10000;separate[2].frames=10000;assert(valid_graph(separate));assert(valid_graph({{1,false,{1,2}},{0,true,{}},{0,true,{}}}));}
    for(size_t choice:{size_t(1),size_t(2)}){Player p;Fake f;f.choose=true;f.chosen=choice;f.complete_at=1;assert(p.start(graph,f,1,false));p.tick(1);assert(p.step()==choice);p.tick(1);assert(p.step()==3);p.tick(1);assert(f.finished==1&&f.released==1&&(f.visits==std::vector<size_t>{0,choice,3}));}
    {auto bad=graph;bad[0].successors={1,1};assert(!valid_graph(bad));bad=graph;bad[1].successors={0};assert(!valid_graph(bad));bad=graph;bad[2].successors={99};assert(!valid_graph(bad));bad=graph;bad[0].successors={2};assert(!valid_graph(bad));bad=graph;bad[2].frames=18000;assert(!valid_graph(bad));bad=graph;bad[3].terminal=false;bad[3].frames=1;assert(!valid_graph(bad));}
    {Player p;Fake f;f.choose=true;f.chosen=99;f.complete_at=1;assert(p.start(graph,f,1,false));p.tick(1);assert(!p.active()&&f.finished==0&&f.reason==StopReason::Failed);}
    {Player p;Fake f;f.choose=true;f.chosen=2;assert(p.start(graph,f,1,false));p.tick(2);assert(f.reason==StopReason::StateLoaded&&f.finished==0);assert(p.start(graph,f,2,false));p.tick(2,false,true);assert(f.reason==StopReason::Skipped&&f.finished==0);}
    {RequestQueue q;assert(q.push(10)&&q.push(20));assert(!q.push(20)&&!q.push(30,30));assert(q.pop()==10);q.discard([](size_t n){return n==20;});assert(!q.pop());for(size_t n=0;n<16;++n)assert(q.push(n));assert(!q.push(99));q.clear();assert(q.size()==0&&!q.pop());assert(q.push(42)&&q.pop()==42);}
    std::puts("story scene lifecycle tests passed");
}
