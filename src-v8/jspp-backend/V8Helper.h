#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Fwd.h"

JSPP_WARNING_GUARD_BEGIN
#include "v8-context.h"
#include "v8-isolate.h"
#include "v8-local-handle.h"
JSPP_WARNING_GUARD_END

#include <tuple>

namespace jspp::v8_backend {

struct V8Helper {
    V8Helper() = delete;

    [[nodiscard]] static v8::Isolate* currentIsolateChecked();

    [[nodiscard]] static v8::Local<v8::Context> currentContextChecked();

    [[nodiscard]] static std::tuple<v8::Isolate*, v8::Local<v8::Context>> currentIsolateContextChecked();

    /**
     * Re-throw the exception in v8::TryCatch as a Exception
     */
    static void rethrowException(v8::TryCatch const& tryCatch);

    static void rethrowToScript(Exception const& exception);

    static void rethrowToScript(v8::TryCatch& tryCatch);

    [[nodiscard]] constexpr inline static v8::PropertyAttribute castAttribute(jspp::PropertyAttribute attribute) {
        static_assert((int)PropertyAttribute::None == (int)v8::None, "Attribute mismatch: None");
        static_assert((int)PropertyAttribute::ReadOnly == (int)v8::ReadOnly, "Attribute mismatch: ReadOnly");
        static_assert((int)PropertyAttribute::DontEnum == (int)v8::DontEnum, "Attribute mismatch: DontEnum");
        static_assert((int)PropertyAttribute::DontDelete == (int)v8::DontDelete, "Attribute mismatch: DontDelete");
        return static_cast<v8::PropertyAttribute>(attribute);
    }
};

} // namespace jspp::v8_backend