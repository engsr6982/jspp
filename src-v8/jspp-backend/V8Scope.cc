#include "V8Scope.h"

#include "jspp/core/Engine.h"

namespace jspp::v8_backend {

V8EngineScope::V8EngineScope(Engine& current, Engine* /* prev */)
: locker_(current.isolate_),
  isolateScope_(current.isolate_),
  handleScope_(current.isolate_),
  contextScope_(current.context_.Get(current.isolate_)) {}

V8ExitEngineScope::V8ExitEngineScope(Engine& current) : unlocker_(current.isolate_) {}

V8EscapeScope::V8EscapeScope(Engine& current) : handleScope_(current.isolate_) {}

} // namespace jspp::v8_backend