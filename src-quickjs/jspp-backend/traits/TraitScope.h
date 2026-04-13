#pragma once
#include "jspp/core/Fwd.h"

#include "../QjsScope.h"

namespace jspp::internal {

template <>
struct ImplType<EngineScope> {
    using type = qjs_backend::QjsEngineScope;
};

template <>
struct ImplType<ExitEngineScope> {
    using type = qjs_backend::QjsExitEngineScope;
};

template <>
struct ImplType<StackFrameScope> {
    using type = qjs_backend::QjsStackFrameScope;
};

} // namespace jspp::internal