#pragma once
#include "jspp/core/Fwd.h"


namespace jspp {

template <>
struct internal::ImplType<Arguments> {
    using type = int; // TODO: please replace with actual type
};

} // namespace jspp