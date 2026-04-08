#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Concepts.h"
#include "jspp/core/Fwd.h"


namespace jspp::stub_backend {


class StubEngineScope {
    // TODO: please implement this

public:
    explicit StubEngineScope(Engine& current, Engine* prev);
    ~StubEngineScope() = default;
};

class StubExitEngineScope {
    // TODO: please implement this

public:
    explicit StubExitEngineScope(Engine& current);
    ~StubExitEngineScope() = default;
};

class StubStackFrameScope {
    // TODO: please implement this

public:
    explicit StubStackFrameScope(Engine& current);
    ~StubStackFrameScope() = default;

    template <concepts::WrapType T>
    Local<T> escape(Local<T> value);
};


} // namespace jspp::stub_backend

#include "StubScope.inl"