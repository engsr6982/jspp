#pragma once
#include "jspp/core/Fwd.h"

#include <utility>

JSPP_WARNING_GUARD_BEGIN
#include <v8-function-callback.h>
JSPP_WARNING_GUARD_END

namespace jspp {

template <>
struct internal::ImplType<Arguments> {
    using type = std::pair<Engine*, v8::FunctionCallbackInfo<v8::Value>>;
};

} // namespace jspp