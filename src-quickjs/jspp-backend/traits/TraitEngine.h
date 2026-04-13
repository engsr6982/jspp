#pragma once
#include "jspp/core/Fwd.h"

#include "../QjsEngine.h"

namespace jspp::internal {


template <>
struct ImplType<Engine> {
    using type = qjs_backend::QjsEngine;
};


} // namespace jspp::internal