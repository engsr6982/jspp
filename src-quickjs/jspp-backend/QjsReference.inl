#pragma once
#include "QjsHelper.h"
#include "jspp/Macro.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

#include "jspp-backend/traits/TraitReference.h"

JSPP_WARNING_GUARD_BEGIN
#include "quickjs.h"
JSPP_WARNING_GUARD_END

#include <utility>

namespace jspp {

namespace qjs_backend {

QjsGlobalRef::QjsGlobalRef() noexcept = default;

QjsGlobalRef::QjsGlobalRef(Engine* engine, JSValue ref) noexcept : ref_(ref), engine_(engine) {}

template <typename T>
QjsGlobalRef::QjsGlobalRef(Engine* engine, Local<T> local)
: QjsGlobalRef(engine, qjs_backend::QjsHelper::getDupLocal(local)) {}

QjsGlobalRef::QjsGlobalRef(QjsGlobalRef&& other) noexcept {
    ref_          = other.ref_;
    engine_       = other.engine_;
    other.engine_ = nullptr;
    other.ref_    = JS_UNDEFINED;
}

QjsGlobalRef& QjsGlobalRef::operator=(QjsGlobalRef&& other) noexcept {
    if (this != &other) {
        reset(); // free old ref
        ref_          = other.ref_;
        engine_       = other.engine_;
        other.engine_ = nullptr;
        other.ref_    = JS_UNDEFINED;
    }
    return *this;
}

void QjsGlobalRef::reset() noexcept {
    if (!JS_IsUndefined(ref_) && !JS_IsNull(ref_) && engine_) {
        EngineScope lock{engine_};
        qjs_backend::QjsHelper::freeValue(ref_);
        ref_    = JS_UNDEFINED;
        engine_ = nullptr;
    }
}

void QjsGlobalRef::reset(Engine* engine, JSValue ref) noexcept {
    reset();
    ref_    = ref;
    engine_ = engine;
}

bool QjsGlobalRef::isEmpty() const noexcept { return JS_IsUndefined(ref_) || JS_IsNull(ref_) || !engine_; }

} // namespace qjs_backend


// Global<T>
template <typename T>
Global<T>::Global() noexcept = default;

template <typename T>
Global<T>::Global(Local<T> const& val) : impl(EngineScope::currentEngine(), val) {}

template <typename T>
Global<T>::Global(Weak<T>&& val) : impl(EngineScope::currentEngine(), val.get()) {}

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
    impl.reset();
}

template <typename T>
Local<T> Global<T>::get() const {
    if (isEmpty()) {
        throw std::runtime_error{"Global<T>::get<T>() called on empty global reference"};
    }
    return ValueHelper::wrap<T>(qjs_backend::QjsHelper::dupValue(impl.ref_));
}

template <typename T>
Local<Value> Global<T>::getValue() const {
    if (isEmpty()) {
        return {}; // undefined
    }
    return ValueHelper::wrap<Value>(qjs_backend::QjsHelper::dupValue(impl.ref_));
}

template <typename T>
bool Global<T>::isEmpty() const {
    return impl.isEmpty();
}

template <typename T>
void Global<T>::reset() {
    impl.reset();
}
template <typename T>
void Global<T>::reset(Local<T> const& val) {
    impl.reset(&EngineScope::currentEngineChecked(), qjs_backend::QjsHelper::getDupLocal(val));
}
template <typename T>
Engine* Global<T>::engine() const {
    return impl.engine_;
}


// Weak<T>
template <typename T>
Weak<T>::Weak() noexcept = default;

template <typename T>
Weak<T>::Weak(Local<T> const& val) : impl(EngineScope::currentEngine(), val) {}

template <typename T>
Weak<T>::Weak(Global<T>&& val) : impl(EngineScope::currentEngine(), val.get()) {}

template <typename T>
Weak<T>::Weak(Weak<T>&& other) noexcept {
    impl = std::move(other.impl);
}

template <typename T>
Weak<T>& Weak<T>::operator=(Weak<T>&& other) noexcept {
    impl = std::move(other.impl);
    return *this;
}

template <typename T>
Weak<T>::~Weak() {
    impl.reset();
}

template <typename T>
Local<T> Weak<T>::get() const {
    if (isEmpty()) {
        throw std::runtime_error{"Weak<T>::get<T>() called on empty weak reference"};
    }
    return ValueHelper::wrap<T>(qjs_backend::QjsHelper::dupValue(impl.ref_));
}

template <typename T>
Local<Value> Weak<T>::getValue() const {
    if (isEmpty()) {
        return {}; // undefined
    }
    return ValueHelper::wrap<Value>(qjs_backend::QjsHelper::dupValue(impl.ref_));
}

template <typename T>
bool Weak<T>::isEmpty() const {
    return impl.isEmpty();
}

template <typename T>
void Weak<T>::reset() {
    impl.reset();
}
template <typename T>
void Weak<T>::reset(Local<T> const& val) {
    impl.reset(&EngineScope::currentEngineChecked(), qjs_backend::QjsHelper::getDupLocal(val));
}
template <typename T>
Engine* Weak<T>::engine() const {
    return impl.engine_;
}


} // namespace jspp