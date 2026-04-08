#pragma once
#include "jspp/core/Fwd.h"

#include "../V8Scope.h"

namespace jspp::internal {

template <>
struct ImplType<EngineScope> {
    using type = v8_backend::V8EngineScope;
};

template <>
struct ImplType<ExitEngineScope> {
    using type = v8_backend::V8ExitEngineScope;
};

template <>
struct ImplType<StackFrameScope> {
    using type = v8_backend::V8EscapeScope;
};

} // namespace jspp::internal