#include "Trampoline.h"
#include "Reference.h"
#include "Value.h"
#include "v8kit/core/ValueHelper.h"

namespace v8kit {

Engine* enable_trampoline::getEngine() const { return engine_; }

Local<Value> enable_trampoline::getThis() const {
    if (!object_ || !engine_ || object_->weak().isEmpty()) {
        return {};
    }
    return object_->weak().get();
}

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


thread_local decltype(TrampolineGuard::gTrampolineStack) TrampolineGuard::gTrampolineStack = nullptr;

TrampolineGuard::TrampolineGuard(const void* o, const void* m) {
    // 向上遍历栈，检查 [同一个对象] 的 [同一个方法] 是否已经在调用中
    for (auto* p = gTrampolineStack; p; p = p->prev) {
        if (p->obj == o && p->methodId == m) {
            is_recursive = true;
            return;
        }
    }
    // 入栈
    frame            = {o, m, gTrampolineStack};
    gTrampolineStack = &frame;
}
TrampolineGuard::~TrampolineGuard() {
    if (!is_recursive) {
        gTrampolineStack = frame.prev;
    }
}

} // namespace v8kit