#include "TrackedHandle.h"

#include "Engine.h"

namespace v8kit {


ITrackedHandle::~ITrackedHandle() { untrack(); }

void ITrackedHandle::track(Engine* engine) {
    if (engine != nullptr) {
        engine_ = engine;
        engine->addTrackedHandle(this);
    }
}

void ITrackedHandle::untrack() {
    if (engine_ != nullptr) {
        engine_->removeTrackedHandle(this);
    }
}

} // namespace v8kit