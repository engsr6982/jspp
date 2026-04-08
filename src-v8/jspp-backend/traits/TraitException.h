#pragma once
#include "jspp/core/Fwd.h"

#include <string>

JSPP_WARNING_GUARD_BEGIN
#include <v8-exception.h>
#include <v8-persistent-handle.h>
#include <v8-value.h>
JSPP_WARNING_GUARD_END


namespace jspp {

namespace v8_backend {
struct V8ExceptionContext {
    ExceptionType                 type{ExceptionType::Unknown};
    mutable std::string           message{};
    mutable v8::Global<v8::Value> error{};

    void extractMessage() const noexcept;
};
} // namespace v8_backend

template <>
struct internal::ImplType<Exception> {
    using type = v8_backend::V8ExceptionContext;
};


} // namespace jspp