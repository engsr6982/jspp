#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"


namespace jspp {

namespace v8_backend {

/**
 * @brief PromiseResolver storage for the V8 backend.
 * @note  v8::Promise::Resolver wraps the same object as the promise it creates,
 *        so a single Global<Promise> is sufficient; settle paths cast back to
 *        v8::Promise::Resolver (same pattern as V8Engine.cc dynamic import).
 */
struct V8ResolverStorage {
    Global<Promise> promise_;
};

} // namespace v8_backend


namespace internal {

template <>
struct ImplType<jspp::PromiseResolver> {
    using type = v8_backend::V8ResolverStorage;
};

} // namespace internal

} // namespace jspp
