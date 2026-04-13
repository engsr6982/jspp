#pragma once
#include "Concepts.h"

#include "jspp-backend/traits/TraitReference.h"


namespace jspp {


struct ValueHelper {
    ValueHelper() = delete;

    template <concepts::WrapType T>
    [[nodiscard]] inline static auto unwrap(Local<T> const& value) {
        return value.val; // friend
    }

    template <concepts::WrapType T>
    [[nodiscard]] inline static Local<T> wrap(typename internal::ImplType<Local<T>>::type value) {
        return Local<T>{value};
    }
};


} // namespace jspp
