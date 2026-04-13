#pragma once
#include "jspp/core/Concepts.h"
#include "jspp/core/Fwd.h"


namespace jspp::qjs_backend {


class QjsEngineScope {
    Engine* current_;
    Engine* prev_;

public:
    explicit QjsEngineScope(Engine& current, Engine* prev);
    ~QjsEngineScope();
};

class QjsExitEngineScope {
    Engine* current_;

public:
    explicit QjsExitEngineScope(Engine& current);
    ~QjsExitEngineScope();
};

class QjsStackFrameScope {
public:
    explicit QjsStackFrameScope(Engine& current);
    ~QjsStackFrameScope() = default;

    template <concepts::WrapType T>
    Local<T> escape(Local<T> value) {
        return value;
    }
};


} // namespace jspp::qjs_backend
