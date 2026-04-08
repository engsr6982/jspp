#pragma once
#include "jspp/core/Fwd.h"

#include "../StubEngine.h"

namespace jspp::internal {


template <>
struct ImplType<Engine> {
    using type = stub_backend::StubEngine;
};


} // namespace jspp::internal