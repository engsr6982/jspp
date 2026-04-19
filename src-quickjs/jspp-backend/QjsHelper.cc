#include "QjsHelper.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Fwd.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"
#include "quickjs.h"
#include <string>

namespace jspp::qjs_backend {

JSRuntime* QjsHelper::currentRuntimeChecked() { return EngineScope::currentEngineChecked().runtime_; }

JSContext* QjsHelper::currentContextChecked() { return EngineScope::currentEngineChecked().context_; }

void QjsHelper::rethrowException(JSValueConst val) {
    if (JS_IsException(val)) {
        rethrowException(-1);
    }
}

void QjsHelper::rethrowException(int code, std::string_view message) {
    if (code < 0) {
        auto& engine = EngineScope::currentEngineChecked();
        auto  error  = JS_GetException(engine.context_);

        if (JS_IsObject(error)) {
            throw Exception{ValueHelper::wrap<Value>(error)};
        } else {
            JS_FreeValue(engine.context_, error);
            throw Exception{std::string{message}};
        }
    }
}

JSValue QjsHelper::rethrowToScript(Exception const& exception, Engine* engine) {
    JSContext* ctx = engine ? engine->context_ : currentContextChecked();
    JS_Throw(ctx, getDupLocal(exception.exception()));
    return JS_EXCEPTION;
}
JSValue QjsHelper::rethrowToScript(std::exception const& exception, Engine* engine) {
    JSContext* ctx = engine ? engine->context_ : currentContextChecked();
    JS_ThrowPlainError(ctx, "C++ Exception: %s", exception.what());
    return JS_EXCEPTION;
}

JSValue QjsHelper::dupValue(JSValueConst val, JSContext* ctx) {
    if (JS_VALUE_HAS_REF_COUNT(val)) {
        return JS_DupValue(ctx ? ctx : currentContextChecked(), val);
    }
    return val;
}

void QjsHelper::freeValue(JSValueConst val, JSContext* ctx) {
    if (JS_VALUE_HAS_REF_COUNT(val)) {
        JS_FreeValue(ctx ? ctx : currentContextChecked(), val);
    }
}

Arguments QjsHelper::wrapArguments(Engine* engine, JSValueConst thiz, int argc, JSValueConst* argv) {
    auto data = internal::ImplType<Arguments>::type{engine, thiz, argc, argv};
    return Arguments{data};
}

} // namespace jspp::qjs_backend