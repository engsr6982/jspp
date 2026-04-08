#include "Trampoline.h"
#include "Reference.h"
#include "Value.h"
#include "jspp/core/Engine.h"
#include "jspp/core/ValueHelper.h"

namespace jspp {

Engine* enable_trampoline::getEngine() const { return engine_; }

Local<Value> enable_trampoline::getThis() const {
    if (!object_ || !engine_ || object_->weak().isEmpty()) {
        return {};
    }
    return object_->weak().get();
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

} // namespace jspp