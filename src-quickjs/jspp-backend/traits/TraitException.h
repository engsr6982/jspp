#pragma once
#include "jspp/core/Fwd.h"
#include "jspp/core/Reference.h"

#include <string>

namespace jspp {

namespace qjs_backend {
struct QjsExceptionContext {
    ExceptionType         type{ExceptionType::Unknown};
    mutable std::string   message{};
    mutable Global<Value> error{};

    void extractMessage() const noexcept;
};
} // namespace qjs_backend

template <>
struct internal::ImplType<Exception> {
    using type = qjs_backend::QjsExceptionContext;
};


} // namespace jspp