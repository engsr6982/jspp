#pragma once
#include "Reference.h" // NOLINT

#include <stdexcept>

namespace jspp {

// Separate this function due to the order of template instantiation
// Ensure that the handle returned by this function is visible
template <typename T>
    requires concepts::WrapType<T>
Local<T> Local<Value>::as() const {
    if constexpr (std::is_same_v<T, Value>) {
        return asValue();
    } else if constexpr (std::is_same_v<T, Null>) {
        return asNull();
    } else if constexpr (std::is_same_v<T, Undefined>) {
        return asUndefined();
    } else if constexpr (std::is_same_v<T, Boolean>) {
        return asBoolean();
    } else if constexpr (std::is_same_v<T, Number>) {
        return asNumber();
    } else if constexpr (std::is_same_v<T, BigInt>) {
        return asBigInt();
    } else if constexpr (std::is_same_v<T, String>) {
        return asString();
    } else if constexpr (std::is_same_v<T, Symbol>) {
        return asSymbol();
    } else if constexpr (std::is_same_v<T, Function>) {
        return asFunction();
    } else if constexpr (std::is_same_v<T, Object>) {
        return asObject();
    } else if constexpr (std::is_same_v<T, Array>) {
        return asArray();
    }
    [[unlikely]] throw std::logic_error("Unable to convert Local<Value> to T, forgot to add if branch?");
}

} // namespace jspp