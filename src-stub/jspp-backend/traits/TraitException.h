#pragma once
#include "jspp/core/Fwd.h"
#include "jspp/core/Reference.h"

#include <string>

namespace jspp {

namespace stub_backend {
struct StubExceptionContext {
    ExceptionType         type{ExceptionType::Unknown};
    mutable std::string   message{};
    mutable Global<Value> error{};

    void extractMessage() const noexcept;
};
} // namespace stub_backend

template <>
struct internal::ImplType<Exception> {
    using type = stub_backend::StubExceptionContext;
};


} // namespace jspp