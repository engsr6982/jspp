#pragma once
#include "TypeConverter.h"
#include "jspp/core/Reference.h"

namespace jspp::binding {

template <typename... Args>
Local<Value> call(Local<Function> const& func, Local<Value> const& thisArg, Args&&... args) {
    std::array<Local<Value>, sizeof...(Args)> argv = {toJs(std::forward<Args>(args))...};
    return func.call(thisArg, std::span<const Local<Value>>(argv));
}

template <typename... Args>
Local<Value> callAsConstructor(Local<Function> const& func, Args&&... args) {
    std::array<Local<Value>, sizeof...(Args)> argv = {toJs(std::forward<Args>(args))...};
    return func.callAsConstructor(std::span<const Local<Value>>(argv));
}

template <concepts::Callable F>
FunctionCallback cpp_func(F&& fn, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic) {
    return adapter::wrapFunction(std::forward<F>(fn), policy);
}

template <concepts::Callable... F>
FunctionCallback overload_func(F&&... args) {
    return adapter::wrapOverloadFuncAndExtraPolicy(std::forward<F>(args)...);
}


} // namespace jspp::binding
