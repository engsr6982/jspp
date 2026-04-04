#include "EngineScope.h"
#include "Engine.h"
#include "NativeInstance.h"

#include <stdexcept>


namespace jspp {

thread_local EngineScope* EngineScope::gCurrentScope_ = nullptr;

EngineScope::EngineScope(Engine& runtime) : EngineScope(&runtime) {}
EngineScope::EngineScope(Engine* runtime)
: engine_(runtime),
  prev_(gCurrentScope_),
  locker_(runtime->isolate_),
  isolateScope_(runtime->isolate_),
  handleScope_(runtime->isolate_),
  contextScope_(runtime->context_.Get(runtime->isolate_)) {
    gCurrentScope_ = this;
}

EngineScope::~EngineScope() { gCurrentScope_ = prev_; }

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

std::tuple<v8::Isolate*, v8::Local<v8::Context>> EngineScope::currentIsolateAndContextChecked() {
    auto& current = currentEngineChecked();
    return std::make_tuple(current.isolate_, current.context_.Get(current.isolate_));
}

v8::Isolate*           EngineScope::currentEngineIsolateChecked() { return currentEngineChecked().isolate_; }
v8::Local<v8::Context> EngineScope::currentEngineContextChecked() {
    auto& current = currentEngineChecked();
    return current.context_.Get(current.isolate_);
}
void EngineScope::ensureEngine(Engine* engine) {
    if (engine == nullptr) {
        throw std::logic_error("An EngineScope must be created before accessing the engine API");
    }
}


ExitEngineScope::ExitEngineScope() : unlocker_(EngineScope::currentEngineChecked().isolate_) {}


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

namespace internal {

V8EscapeScope::V8EscapeScope() : handleScope_(EngineScope::currentEngineChecked().isolate_) {}
V8EscapeScope::V8EscapeScope(v8::Isolate* isolate) : handleScope_(isolate) {}

} // namespace internal


} // namespace jspp