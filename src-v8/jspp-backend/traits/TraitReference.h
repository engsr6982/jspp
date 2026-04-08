#pragma once

#include "jspp/Macro.h"
#include "jspp/core/Fwd.h"

JSPP_WARNING_GUARD_BEGIN
#include <v8-function.h>
#include <v8-local-handle.h>
#include <v8-object.h>
#include <v8-primitive.h>
#include <v8-value.h>
JSPP_WARNING_GUARD_END


namespace jspp {


namespace internal {
template <typename T>
struct V8TypeAlias {
    static_assert(sizeof(T) == 0, "V8TypeAlias not defined for this type");
};

template <typename T>
using V8Type_v = typename V8TypeAlias<T>::type;

#define TYPE_ALIAS(WRAP_TYPE, V8_TYPE)                                                                                 \
    template <>                                                                                                        \
    struct V8TypeAlias<WRAP_TYPE> {                                                                                    \
        using type = V8_TYPE;                                                                                          \
    };

TYPE_ALIAS(Value, v8::Value);
TYPE_ALIAS(Null, v8::Primitive);
TYPE_ALIAS(Undefined, v8::Primitive);
TYPE_ALIAS(Boolean, v8::Boolean);
TYPE_ALIAS(Number, v8::Number);
TYPE_ALIAS(BigInt, v8::BigInt);
TYPE_ALIAS(String, v8::String);
TYPE_ALIAS(Symbol, v8::Symbol);
TYPE_ALIAS(Function, v8::Function);
TYPE_ALIAS(Object, v8::Object);
TYPE_ALIAS(Array, v8::Array);

#undef TYPE_ALIAS

} // namespace internal


namespace v8_backend {
template <typename T>
struct V8GlobalRef {
    using v8Ref = v8::Global<internal::V8Type_v<T>>;

    v8Ref   ref_;
    Engine* engine_{nullptr};

public:
    V8GlobalRef() = default;
    V8GlobalRef(Engine* engine, v8Ref ref);
    V8GlobalRef(Engine* engine, Local<T> local);

    JSPP_DISABLE_COPY(V8GlobalRef);

    V8GlobalRef(V8GlobalRef&& other) noexcept {
        ref_          = std::move(other.ref_);
        engine_       = other.engine_;
        other.engine_ = nullptr;
    }

    V8GlobalRef& operator=(V8GlobalRef&& other) noexcept {
        if (this != &other) {
            ref_          = std::move(other.ref_);
            engine_       = other.engine_;
            other.engine_ = nullptr;
        }
        return *this;
    }

    void markWeak() {
        if (!ref_.IsEmpty()) {
            ref_.SetWeak();
        }
    }
};
} // namespace v8_backend


namespace internal {

template <typename T>
struct ImplType<Local<T>> {
    using type = v8::Local<V8Type_v<T>>;
};

template <typename T>
struct ImplType<Global<T>> {
    using type = v8_backend::V8GlobalRef<T>;
};

template <typename T>
struct ImplType<Weak<T>> {
    using type = v8_backend::V8GlobalRef<T>;
};

} // namespace internal

} // namespace jspp