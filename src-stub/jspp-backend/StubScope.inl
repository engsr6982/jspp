#pragma once
#include "StubScope.h"

#include "jspp/core/ValueHelper.h"

namespace jspp::stub_backend {


template <concepts::WrapType T>
Local<T> StubStackFrameScope::escape(Local<T> value) {
    // TODO: Please check if the target engine has this feature.
    return value;
}


} // namespace jspp::stub_backend