#pragma once
#include "jspp/core/Fwd.h"

#include "../StubScope.h"

namespace jspp::internal {

template <>
struct ImplType<EngineScope> {
    using type = stub_backend::StubEngineScope;
};

template <>
struct ImplType<ExitEngineScope> {
    using type = stub_backend::StubExitEngineScope;
};

template <>
struct ImplType<StackFrameScope> {
    using type = stub_backend::StubStackFrameScope;
};

} // namespace jspp::internal