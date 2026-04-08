#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Fwd.h"

#include <tuple>

namespace jspp::stub_backend {

struct StubHelper {
    StubHelper() = delete;

    static void rethrowToScript(Exception const& exception);

    // TODO: please implement this
    // [[nodiscard]] constexpr inline static <flags> castAttribute(jspp::PropertyAttribute attribute)
};

} // namespace jspp::stub_backend