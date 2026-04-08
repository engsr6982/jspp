#include "V8Helper.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

#include <v8-exception.h>

namespace jspp::v8_backend {

v8::Isolate* V8Helper::currentIsolateChecked() { return EngineScope::currentEngineChecked().isolate_; }

v8::Local<v8::Context> V8Helper::currentContextChecked() { return EngineScope::currentEngineChecked().context(); }

std::tuple<v8::Isolate*, v8::Local<v8::Context>> V8Helper::currentIsolateContextChecked() {
    auto& engine = EngineScope::currentEngineChecked();
    return std::make_tuple(engine.isolate_, engine.context());
}

void V8Helper::rethrowException(v8::TryCatch const& tryCatch) {
    if (tryCatch.HasCaught()) {
        throw Exception(ValueHelper::wrap<Value>(tryCatch.Exception()));
    }
}

void V8Helper::rethrowToScript(Exception const& exception) {
    auto isolate = currentIsolateChecked();
    isolate->ThrowException(ValueHelper::unwrap(exception.exception()));
}

void V8Helper::rethrowToScript(v8::TryCatch& tryCatch) {
    if (tryCatch.HasCaught()) {
        tryCatch.ReThrow();
    }
}


} // namespace jspp::v8_backend