#include "jspp/core/Engine.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Trampoline.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"


namespace jspp {

Local<Value> enable_trampoline::getOverride(Local<String> const& methodName) const {
    if (!object_ || !engine_ || object_->weak().isEmpty()) {
        return {};
    }
    auto This = object_->weak().get();

    auto overrideFunc = This.get(methodName);
    if (overrideFunc.isFunction()) {
        auto func = overrideFunc.asFunction();

        auto ctx = engine_->context();
        auto tag = engine_->nativeFunctionTag_.Get(engine_->isolate_);

        auto v8Func = ValueHelper::unwrap(func);
        if (v8Func->HasPrivate(ctx, tag).FromMaybe(false)) {
            return {}; // method 具有 nativeFunctionTag_，说明是原生方法，JS 未进行重写
        }
        return func;
    }
    return {};
}

} // namespace jspp