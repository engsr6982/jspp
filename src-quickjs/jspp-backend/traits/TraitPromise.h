#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"


namespace jspp {

namespace qjs_backend {

/**
 * @brief PromiseResolver storage for the QuickJS backend.
 */
struct QjsResolverStorage {
    Global<Promise>  promise_;
    Global<Function> resolve_; // capability function from JS_NewPromiseCapability
    Global<Function> reject_;
};

} // namespace qjs_backend


namespace internal {

template <>
struct ImplType<jspp::PromiseResolver> {
    using type = qjs_backend::QjsResolverStorage;
};

} // namespace internal

} // namespace jspp
