#pragma once
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Reference.h"
#include "jspp/core/ValueHelper.h"

#include "jspp-backend/traits/TraitReference.h"

#include <utility>

namespace jspp {

// Global<T>
template <typename T>
Global<T>::Global() noexcept = default;

template <typename T>
Global<T>::Global(Local<T> const& val) /* : impl(EngineScope::currentEngine(), val) */ {} // TODO: impl this

template <typename T>
Global<T>::Global(Weak<T>&& val) /* : impl(EngineScope::currentEngine(), std::move(val.impl)) */ {} // TODO: impl this

template <typename T>
Global<T>::Global(Global<T>&& other) noexcept {
    impl = std::move(other.impl);
}

template <typename T>
Global<T>& Global<T>::operator=(Global<T>&& other) noexcept {
    impl = std::move(other.impl);
    return *this;
}

template <typename T>
Global<T>::~Global() {
    reset();
}

template <typename T>
Local<T> Global<T>::get() const {
    return Local<T>{impl}; // TODO: impl this
}

template <typename T>
Local<Value> Global<T>::getValue() const {
    return Local<Value>{impl}; // TODO: impl this
}

template <typename T>
bool Global<T>::isEmpty() const {
    return false; // TODO: impl this
}

template <typename T>
void Global<T>::reset() {
    // TODO: impl this
}
template <typename T>
void Global<T>::reset(Local<T> const& val) {
    // TODO: impl this
    // TODO: if val is empty, reset it
}
template <typename T>
Engine* Global<T>::engine() const {
    return nullptr; // TODO: impl this
}


// Weak<T>
template <typename T>
Weak<T>::Weak() noexcept = default;

template <typename T>
Weak<T>::Weak(Local<T> const& val) /* : impl(EngineScope::currentEngine(), val) */ {
    // TODO: if backend support weak, and need to mark weak, mark weak
}

template <typename T>
Weak<T>::Weak(Global<T>&& val) /* : impl(EngineScope::currentEngine(), std::move(val.impl)) */ {
    // TODO: if backend support weak, and need to mark weak, mark weak
}

template <typename T>
Weak<T>::Weak(Weak<T>&& other) noexcept {
    impl = std::move(other.impl);
    // TODO: if backend support weak, and need to mark weak, mark weak
}

template <typename T>
Weak<T>& Weak<T>::operator=(Weak<T>&& other) noexcept {
    impl = std::move(other.impl);
    // TODO: if backend support weak, and need to mark weak, mark weak
    return *this;
}

template <typename T>
Weak<T>::~Weak() {
    reset();
}

template <typename T>
Local<T> Weak<T>::get() const {
    return Local<T>{impl}; // TODO: impl this
}

template <typename T>
Local<Value> Weak<T>::getValue() const {
    return Local<Value>{impl}; // TODO: impl this
}

template <typename T>
bool Weak<T>::isEmpty() const {
    return false; // TODO: impl this
}

template <typename T>
void Weak<T>::reset() {
    // TODO: impl this
}
template <typename T>
void Weak<T>::reset(Local<T> const& val) {
    // TODO: impl this
    // TODO: if val is empty, reset it
    // TODO: if backend support weak, and need to mark weak, mark weak
}
template <typename T>
Engine* Weak<T>::engine() const {
    return nullptr; // TODO: impl this
}


} // namespace jspp