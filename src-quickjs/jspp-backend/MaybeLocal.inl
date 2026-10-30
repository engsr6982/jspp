#pragma once
#include "jspp/core/MaybeLocal.h" // NOLINT

#include "jspp-backend/QjsHelper.h"

namespace jspp {

template <typename T>
MaybeLocal<T>::MaybeLocal() : MaybeLocal<T>(JS_UNDEFINED) {}

template <typename T>
MaybeLocal<T>::MaybeLocal(Local<T> const& success) : MaybeLocal<T>(qjs_backend::QjsHelper::getDupLocal(success)) {}

template <typename T>
MaybeLocal<T>::~MaybeLocal() {
    if (hasValue()) {
        auto v = std::get<LocalTypeImpl>(impl);
        qjs_backend::QjsHelper::freeValue(v);
    }
}

template <typename T>
MaybeLocal<T>::MaybeLocal(MaybeLocal const& cp) : impl(cp.impl) {
    if (hasValue()) {
        auto v = std::get<LocalTypeImpl>(impl);
        qjs_backend::QjsHelper::dupValue(v);
    }
}

template <typename T>
MaybeLocal<T>& MaybeLocal<T>::operator=(MaybeLocal const& cp) {
    if (&cp != this) {
        MaybeLocal<T> tmp(cp);
        *this = std::move(tmp);
    }
    return *this;
}

template <typename T>
MaybeLocal<T>::MaybeLocal(MaybeLocal&& mv) noexcept : impl(std::move(mv.impl)) {
    mv.impl.template emplace<LocalTypeImpl>(JS_UNDEFINED);
}

template <typename T>
MaybeLocal<T>& MaybeLocal<T>::operator=(MaybeLocal&& mv) noexcept {
    if (&mv != this) {
        // Release the previous value owned by this object.
        if (hasValue()) {
            auto v = std::get<LocalTypeImpl>(impl);
            qjs_backend::QjsHelper::freeValue(v);
        }

        // Transfer ownership.
        impl = std::move(mv.impl);

        // Leave the source in a valid state.
        mv.impl.template emplace<LocalTypeImpl>(JS_UNDEFINED);
    }
    return *this;
}

template <typename T>
Local<T> MaybeLocal<T>::toLocal() const {
    if (hasValue()) {
        auto v = std::get<LocalTypeImpl>(impl);
        return Local<T>{qjs_backend::QjsHelper::dupValue(v)};
    }
    throw toException();
}

} // namespace jspp