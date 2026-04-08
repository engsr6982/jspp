#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Fwd.h"


namespace jspp {


namespace internal {

template <typename T>
struct ImplType<Local<T>> {
    using type = int; // TODO: please replace with actual type
};

template <typename T>
struct ImplType<Global<T>> {
    using type = int; // TODO: please replace with actual type
};

template <typename T>
struct ImplType<Weak<T>> {
    using type = int; // TODO: please replace with actual type
};

} // namespace internal

} // namespace jspp