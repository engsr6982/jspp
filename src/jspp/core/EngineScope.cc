#include "EngineScope.h"
#include "Engine.h"
#include "NativeInstance.h"

#include <stdexcept>


namespace jspp {

thread_local EngineScope* EngineScope::gCurrentScope_ = nullptr;

EngineScope::EngineScope(Engine& engine) : EngineScope(&engine) {}
EngineScope::EngineScope(Engine* engine) : EngineScope(InternalEnterFlag{}, engine) {
    if (engine_ == nullptr) {
        throw std::logic_error("An EngineScope must be created with an Engine");
    }
}
EngineScope::EngineScope(InternalEnterFlag, Engine* engine, bool needEnter) : engine_(engine), prev_(gCurrentScope_) {
    auto prevEngine = prev_ != nullptr ? prev_->engine_ : nullptr;
    needEnter_      = needEnter && engine_ != nullptr && prevEngine != engine;
    if (needEnter_) {
        // todo: check destroying engine?
        impl_.emplace(*engine, prevEngine);
    }
    gCurrentScope_ = this;
}

EngineScope::~EngineScope() {
    if (needEnter_) {
        impl_.reset();
    }
    gCurrentScope_ = prev_;
}

Engine* EngineScope::currentEngine() {
    if (gCurrentScope_) {
        return const_cast<Engine*>(gCurrentScope_->engine_);
    }
    return nullptr;
}

Engine& EngineScope::currentEngineChecked() {
    auto current = currentEngine();
    ensureEngine(current);
    return *current;
}

void EngineScope::ensureEngine(Engine* engine) {
    if (engine == nullptr) {
        throw std::logic_error("An EngineScope must be created before accessing the engine API");
    }
}

// ExitEngineScope
ExitEngineScope::ExitEngineScope()
: holder_(EngineScope::currentEngine()),
  nullScope_(EngineScope::InternalEnterFlag{}, nullptr) {}

ExitEngineScope::ExitHolder::ExitHolder(Engine* engine) {
    if (engine) {
        impl_.emplace(*engine);
    }
}

ExitEngineScope::~ExitEngineScope() = default;


// StackFrameScope
StackFrameScope::StackFrameScope(Engine& engine) : impl_(engine) {}
StackFrameScope::~StackFrameScope() = default;


// TransientObjectScope
thread_local TransientObjectScope* TransientObjectScope::gCurrentScope_ = nullptr;

TransientObjectScope::TransientObjectScope() : prev_(gCurrentScope_) { gCurrentScope_ = this; }
TransientObjectScope::~TransientObjectScope() {
    gCurrentScope_ = prev_;
    for (auto instance : trackedInstances_) {
        instance->invalidate();
    }
}
void                  TransientObjectScope::track(NativeInstance* instance) { trackedInstances_.push_back(instance); }
bool                  TransientObjectScope::isActive() { return gCurrentScope_ != nullptr; }
TransientObjectScope* TransientObjectScope::current() { return gCurrentScope_; }
TransientObjectScope& TransientObjectScope::currentChecked() {
    auto scope = current();
    if (!scope) throw std::logic_error("No TransientNativeScope is active");
    return *scope;
}


} // namespace jspp