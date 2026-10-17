#include "gfx/vulkan/result_observer.h"
#include <cassert>
#include <initializer_list>
enum class Result { success, not_ready, lost, other_error };
static int noted=0;
static void note() {++noted;}
static Result actual(Result result) {return result;}
static Result second(Result result) {return result==Result::success?Result::not_ready:result;}
static void noop(int) {}
static Result (*entry)(Result)=nullptr, (*other)(Result)=nullptr;
static void (*command)(int)=nullptr;
int main() {
    using First=gfxvk::detail::ResultObserver<&entry,decltype(entry),Result,Result::lost,note>;
    using Second=gfxvk::detail::ResultObserver<&other,decltype(other),Result,Result::lost,note>;
    using Void=gfxvk::detail::ResultObserver<&command,decltype(command),Result,Result::lost,note>;
    entry=First::bind(actual);other=Second::bind(second);command=Void::bind(noop);
    assert(command==noop);command(0);assert(noted==0);
    for(auto result:{Result::success,Result::not_ready,Result::other_error})assert(entry(result)==result);
    assert(noted==0);
    // Includes a caller that deliberately ignores the failure, as background warmup can do.
    entry(Result::lost);assert(noted==1);
    assert(other(Result::lost)==Result::lost&&noted==2);
    assert(entry(Result::success)==Result::success&&other(Result::success)==Result::not_ready);
    assert(First::bind(nullptr)==nullptr);
    using DeviceCall=gfxvk::detail::ResultFunction<decltype(entry),Result,Result::lost,note>;
    DeviceCall primary(actual),secondary(second);assert(primary&&secondary);
    primary(Result::lost);secondary(Result::lost);assert(noted==4);
    assert(primary(Result::success)==Result::success&&secondary(Result::success)==Result::not_ready);
    secondary=nullptr;assert(!secondary);assert(primary(Result::success)==Result::success&&noted==4);
}
