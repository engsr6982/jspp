#include "QjsHelper.h"
#include "jspp/core/Engine.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Trampoline.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"
#include "quickjs.h"


namespace jspp {

Local<Value> enable_trampoline::getOverride(Local<String> const& methodName) const {
    if (!object_ || !engine_ || object_->weak().isEmpty()) {
        return {};
    }
    auto This = object_->weak().get();

    auto overrideFunc = This.get(methodName);
    if (overrideFunc.isFunction()) {
        auto func = overrideFunc.asFunction();
        auto code =
            JS_HasProperty(engine_->context_, qjs_backend::QjsHelper::peekValue(func), engine_->nativeFunctionTag_);
        qjs_backend::QjsHelper::rethrowException(code);
        if (code == 1) {
            return {}; // has native function tag
        }
        return func;
    }
    return {};
}

} // namespace jspp