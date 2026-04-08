#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Concepts.h"
#include "jspp/core/Fwd.h"

JSPP_WARNING_GUARD_BEGIN
#include <v8-context.h>
#include <v8-isolate.h>
#include <v8-locker.h>
#include <v8.h>
JSPP_WARNING_GUARD_END

namespace jspp::v8_backend {


class V8EngineScope {
    v8::Locker         locker_;
    v8::Isolate::Scope isolateScope_;
    v8::HandleScope    handleScope_;
    v8::Context::Scope contextScope_;

public:
    explicit V8EngineScope(Engine& current, Engine* prev);
    ~V8EngineScope() = default;
};

class V8ExitEngineScope {
    v8::Unlocker unlocker_;

public:
    explicit V8ExitEngineScope(Engine& current);
    ~V8ExitEngineScope() = default;
};

class V8EscapeScope {
    v8::EscapableHandleScope handleScope_;

public:
    explicit V8EscapeScope(Engine& current);
    ~V8EscapeScope() = default;

    template <concepts::WrapType T>
    Local<T> escape(Local<T> value);
};


} // namespace jspp::v8_backend

#include "V8Scope.inl"