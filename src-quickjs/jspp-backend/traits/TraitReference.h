#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Fwd.h"

JSPP_WARNING_GUARD_BEGIN
#include <quickjs.h>
JSPP_WARNING_GUARD_END

namespace jspp {
namespace qjs_backend {
struct QjsGlobalRef {
    JSValue ref_{JS_UNDEFINED};
    Engine* engine_{nullptr};

public:
    inline QjsGlobalRef() noexcept;
    inline QjsGlobalRef(Engine* engine, JSValue ref) noexcept;

    template <typename T>
    inline QjsGlobalRef(Engine* engine, Local<T> local);

    JSPP_DISABLE_COPY(QjsGlobalRef);

    inline QjsGlobalRef(QjsGlobalRef&& other) noexcept;

    inline QjsGlobalRef& operator=(QjsGlobalRef&& other) noexcept;

    inline void reset() noexcept;

    inline void reset(Engine* engine, JSValue ref) noexcept;

    inline bool isEmpty() const noexcept;
};
} // namespace qjs_backend

namespace internal {

template <typename T>
struct ImplType<Local<T>> {
    using type = JSValue;
};

template <typename T>
struct ImplType<Global<T>> {
    using type = qjs_backend::QjsGlobalRef;
};

template <typename T>
struct ImplType<Weak<T>> {
    using type = qjs_backend::QjsGlobalRef;
};

} // namespace internal

} // namespace jspp