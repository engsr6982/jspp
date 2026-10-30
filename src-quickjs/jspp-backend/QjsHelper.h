#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Fwd.h"
#include "jspp/core/ValueHelper.h"

#include <string_view>

JSPP_WARNING_GUARD_BEGIN
#include "quickjs.h"
JSPP_WARNING_GUARD_END

namespace jspp::qjs_backend {

struct QjsHelper {
    QjsHelper() = delete;

    [[nodiscard]] static JSRuntime* currentRuntimeChecked();

    [[nodiscard]] static JSContext* currentContextChecked();

    static bool isException(JSValueConst val);
    static bool isException(int code);

    static void rethrowException(JSValueConst val);
    static void rethrowException(int code, std::string_view message = "Unknown error");

    template <typename T>
    [[nodiscard]] static inline MaybeLocal<T> asMaybeLocal(int code, std::string_view message = "Unknown error") {
        if (isException(code)) {
            auto& engine = EngineScope::currentEngineChecked();
            auto  error  = JS_GetException(engine.context_);

            if (JS_IsObject(error)) {
                return ValueHelper::wrapMaybeLocal<T>(error);
            } else {
                JS_FreeValue(engine.context_, error);
                return MaybeLocal<T>::fromError(std::string{message});
            }
        }
        return {};
    }
    template <typename T>
    [[nodiscard]] static inline MaybeLocal<T> asMaybeLocal(JSValueConst val) {
        if (JS_IsException(val)) {
            return asMaybeLocal<T>(-1);
        }
        return ValueHelper::wrapMaybeLocal<T>(dupValue(val));
    }

    [[nodiscard]] static JSValue rethrowToScript(Exception const& exception, Engine* engine = nullptr);
    [[nodiscard]] static JSValue rethrowToScript(std::exception const& exception, Engine* engine = nullptr);

    [[nodiscard]] static JSValue dupValue(JSValueConst val, JSContext* ctx = nullptr);

    static void freeValue(JSValueConst val, JSContext* ctx = nullptr);

    [[nodiscard]] static Arguments wrapArguments(Engine* engine, JSValueConst thiz, int argc, JSValueConst* argv);

    template <typename T>
    [[nodiscard]] static JSValue peekValue(Local<T> const& local) {
        return ValueHelper::unwrap(local);
    }
    template <typename T>
    [[nodiscard]] static JSValue getDupLocal(Local<T> const& local, JSContext* ctx = nullptr) {
        return dupValue(ValueHelper::unwrap(local), ctx);
    }

    [[nodiscard]] constexpr inline static int castAttribute(jspp::PropertyAttribute attribute) {
        int flags = JS_PROP_C_W_E;
        if ((int)attribute & (int)jspp::PropertyAttribute::ReadOnly) {
            flags &= ~JS_PROP_WRITABLE;
        }
        if ((int)attribute & (int)jspp::PropertyAttribute::DontEnum) {
            flags &= ~JS_PROP_ENUMERABLE;
        }
        if ((int)attribute & (int)jspp::PropertyAttribute::DontDelete) {
            flags &= ~JS_PROP_CONFIGURABLE;
        }
        return flags;
    }
};

} // namespace jspp::qjs_backend