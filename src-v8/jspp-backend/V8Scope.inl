#pragma once
#include "V8Scope.h"

#include "jspp/core/ValueHelper.h"

namespace jspp::v8_backend {


template <concepts::WrapType T>
Local<T> V8EscapeScope::escape(Local<T> value) {
    auto v8local = ValueHelper::unwrap(value);
    return ValueHelper::wrap<T>(handleScope_.Escape(v8local));
}


} // namespace jspp::v8_backend