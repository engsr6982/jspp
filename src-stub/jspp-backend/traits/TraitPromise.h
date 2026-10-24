#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Fwd.h"


namespace jspp {

namespace stub_backend {

struct StubResolverStorage {};

} // namespace stub_backend


namespace internal {

template <>
struct ImplType<jspp::PromiseResolver> {
    using type = stub_backend::StubResolverStorage;
};

} // namespace internal

} // namespace jspp
