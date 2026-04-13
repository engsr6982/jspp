#pragma once
#include "jspp/core/Fwd.h"


namespace jspp {

namespace qjs_backend {
struct ArgumentData {
    Engine*       engine_;
    JSValueConst  thiz_;
    int           argc_;
    JSValueConst* argv_;

    constexpr explicit ArgumentData(Engine* engine, JSValueConst thiz, int argc, JSValueConst* argv)
    : engine_(engine),
      thiz_(thiz),
      argc_(argc),
      argv_(argv) {}
};
} // namespace qjs_backend

template <>
struct internal::ImplType<Arguments> {
    using type = qjs_backend::ArgumentData;
};

} // namespace jspp