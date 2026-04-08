#pragma once
#include "jspp/core/Fwd.h"

#include "../V8Engine.h"

namespace jspp::internal {


template <>
struct ImplType<Engine> {
    using type = v8_backend::V8Engine;
};


} // namespace jspp::internal