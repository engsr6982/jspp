#include "QjsScope.h"

#include "jspp/Macro.h"
#include "jspp/core/Engine.h"

JSPP_WARNING_GUARD_BEGIN
#include "quickjs.h"
JSPP_WARNING_GUARD_END
namespace jspp::qjs_backend {

QjsEngineScope::QjsEngineScope(Engine& current, Engine* prev) : current_(&current), prev_(prev) {
    if (prev) {
        prev->mutex_.unlock();
    }
    current.mutex_.lock();
    JS_UpdateStackTop(current.runtime_);
}
QjsEngineScope::~QjsEngineScope() {
    current_->pumpPendingJobs();
    current_->mutex_.unlock();
    if (prev_) {
        prev_->mutex_.lock();
    }
}


QjsExitEngineScope::QjsExitEngineScope(Engine& current) : current_(&current) { current.mutex_.unlock(); }
QjsExitEngineScope::~QjsExitEngineScope() { current_->mutex_.lock(); }


QjsStackFrameScope::QjsStackFrameScope(Engine& /* current */) {}

} // namespace jspp::qjs_backend