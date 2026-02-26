#pragma once
#include "Fwd.h"
#include "v8kit/core/TrackedHandle.h"

#include <memory>

namespace v8kit {


class enable_trampoline {
    friend class Engine;

    Engine*                              engine_{nullptr};
    std::shared_ptr<TrackedWeak<Object>> object_{nullptr};

protected:
    enable_trampoline()  = default;
    ~enable_trampoline() = default;

    Local<Value> getThis() const;

    Local<Value> getOverride(Local<String> const& methodName) const;
};


struct TrampolineGuard {
    struct Frame {
        const void* obj;
        const void* methodId;
        Frame*      prev;
    };

    bool  is_recursive = false;
    Frame frame{};

    TrampolineGuard(const void* o, const void* m);
    ~TrampolineGuard();

private:
    static thread_local Frame* gTrampolineStack;
};

namespace binding {
template <typename T>
decltype(auto) toCpp(Local<Value> const& value);
template <typename T>
struct OverrideReturner {
    static T convert(Local<Value> const& val) {
        // Delay template instantiation to avoid errors caused by assertions.
        return v8kit::binding::toCpp<T>(val);
    }
};

template <>
struct OverrideReturner<void> {
    static void convert(Local<Value> const&) {}
};

} // namespace binding

} // namespace v8kit

#define V8KIT_OVERRIDE_IMPL(RETURN_TYPE, BASE_CLASS, SCRIPTMETHOD, METHOD, ...)                                        \
    {                                                                                                                  \
        /* 每个成员函数内的 static 变量都有唯一的内存地址 */                                                           \
        /* 利用这个地址作为该方法的 methodId */                                                                        \
        static const char        __v8kit_method_id = 0;                                                                \
        ::v8kit::TrampolineGuard __guard{this, &__v8kit_method_id};                                                    \
        if (!__guard.is_recursive) {                                                                                   \
            auto _maybe_override_ = this->getOverride(::v8kit::String::newString(SCRIPTMETHOD));                       \
            if (_maybe_override_.isFunction()) {                                                                       \
                auto _override_fn_ = _maybe_override_.asFunction();                                                    \
                auto value =                                                                                           \
                    ::v8kit::binding::call(_override_fn_, this->getThis().asObject() __VA_OPT__(, ) __VA_ARGS__);      \
                return ::v8kit::binding::OverrideReturner<RETURN_TYPE>::convert(value);                                \
            }                                                                                                          \
        }                                                                                                              \
    }


#define V8KIT_OVERRIDE(RETURN_TYPE, BASE_CLASS, SCRIPTMETHOD, METHOD, ...)                                             \
    V8KIT_OVERRIDE_IMPL(RETURN_TYPE, BASE_CLASS, SCRIPTMETHOD, METHOD, __VA_ARGS__)                                    \
    /* 1. 如果是递归调用 (super.onLoad) */                                                                             \
    /* 2. 如果 JS 没重写 (nativeFunctionTag_ 命中) */                                                                  \
    /* 则执行 C++ 默认实现 */                                                                                          \
    return BASE_CLASS::METHOD(__VA_ARGS__)

// virtual void xxx() = 0;
#define V8KIT_OVERRIDE_PURE(RETURN_TYPE, BASE_CLASS, SCRIPTMETHOD, METHOD, ...)                                        \
    V8KIT_OVERRIDE_IMPL(RETURN_TYPE, BASE_CLASS, SCRIPTMETHOD, METHOD, __VA_ARGS__)                                    \
    /* 既然是纯虚函数，JS 侧调用 super.method() 应该什么都不做或者抛出异常 */                                          \
    if constexpr (!std::is_void_v<RETURN_TYPE>) {                                                                      \
        throw ::v8kit::Exception(                                                                                      \
            "Tried to call pure virtual function: " #BASE_CLASS "::" #METHOD "(JS: "#SCRIPTMETHOD ")",                \
            ::v8kit::Exception::Type::TypeError                                                                        \
        );                                                                                                             \
    }
