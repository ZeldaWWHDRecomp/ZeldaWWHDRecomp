// Observe a result at the loaded entry point, including callers that recover or ignore errors.
// Non-observed return types retain their original function pointer without a trampoline.
#pragma once
#include <type_traits>
namespace gfxvk::detail {
template<auto Slot,typename Function,typename Observed,Observed Failure,auto Notify>
struct ResultObserver;
template<auto Slot,typename Observed,Observed Failure,auto Notify,typename Result,typename... Args>
struct ResultObserver<Slot,Result(*)(Args...),Observed,Failure,Notify> {
    using Function=Result(*)(Args...);
    static inline Function original=nullptr;
    static Result call(Args... args) {
        if constexpr(std::is_same_v<Result,Observed>) {
            Result result=original(args...);
            if(result==Failure)Notify();
            return result;
        } else return original(args...);
    }
    static Function bind(Function function) {
        if constexpr(std::is_same_v<Result,Observed>) {
            original=function;
            return function?&call:nullptr;
        } else return function;
    }
};
// Per-device dispatch tables retain independent pointers; never share a global slot.
template<typename Function,typename Observed,Observed Failure,auto Notify> struct ResultFunction;
template<typename Observed,Observed Failure,auto Notify,typename Result,typename... Args>
struct ResultFunction<Result(*)(Args...),Observed,Failure,Notify> {
    using Function=Result(*)(Args...);
    Function original=nullptr;
    ResultFunction()=default;
    ResultFunction(Function function):original(function) {}
    ResultFunction& operator=(Function function){original=function;return *this;}
    explicit operator bool() const{return original!=nullptr;}
    Result operator()(Args... args) const {
        if constexpr(std::is_same_v<Result,Observed>) {
            Result result=original(args...);if(result==Failure)Notify();return result;
        } else return original(args...);
    }
};

}
