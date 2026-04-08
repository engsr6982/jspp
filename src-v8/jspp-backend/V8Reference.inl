#pragma once
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Reference.h"
#include "jspp/core/ValueHelper.h"

#include "jspp-backend/traits/TraitReference.h"
#include <utility>

namespace jspp {

namespace v8_backend {
template <typename T>
V8GlobalRef<T>::V8GlobalRef(Engine* engine, v8Ref ref) : ref_(std::move(ref)),
                                                         engine_(engine) {}
template <typename T>
V8GlobalRef<T>::V8GlobalRef(Engine* engine, Local<T> local)
: ref_(engine->isolate(), ValueHelper::unwrap(local)),
  engine_(engine) {}
} // namespace v8_backend

// Global<T>
template <typename T>
Global<T>::Global() noexcept = default;

template <typename T>
Global<T>::Global(Local<T> const& val) : impl(EngineScope::currentEngine(), val) {}

template <typename T>
Global<T>::Global(Weak<T>&& val) : impl(EngineScope::currentEngine(), std::move(val.impl.ref_)) {}

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
    return Local<T>{impl.ref_.Get(impl.engine_->isolate_)};
}

template <typename T>
Local<Value> Global<T>::getValue() const {
    return Local<Value>{impl.ref_.Get(impl.engine_->isolate_).template As<v8::Value>()};
}

template <typename T>
bool Global<T>::isEmpty() const {
    return impl.ref_.IsEmpty();
}

template <typename T>
void Global<T>::reset() {
    impl.ref_.Reset();
    impl.engine_ = nullptr;
}
template <typename T>
void Global<T>::reset(Local<T> const& val) {
    auto raw = ValueHelper::unwrap(val);
    if (raw.IsEmpty()) {
        reset();
        return;
    }
    auto& current = EngineScope::currentEngineChecked();
    impl.ref_.Reset(current.isolate_, raw);
    impl.engine_ = &current;
}
template <typename T>
Engine* Global<T>::engine() const {
    return impl.engine_;
}


// Weak<T>
template <typename T>
Weak<T>::Weak() noexcept = default;

template <typename T>
Weak<T>::Weak(Local<T> const& val) : impl(EngineScope::currentEngine(), val) {
    impl.markWeak();
}

template <typename T>
Weak<T>::Weak(Global<T>&& val) : impl(EngineScope::currentEngine(), std::move(val.impl.ref_)) {
    impl.markWeak();
}

template <typename T>
Weak<T>::Weak(Weak<T>&& other) noexcept {
    impl = std::move(other.impl);
    impl.markWeak();
}

template <typename T>
Weak<T>& Weak<T>::operator=(Weak<T>&& other) noexcept {
    impl = std::move(other.impl);
    impl.markWeak();
    return *this;
}

template <typename T>
Weak<T>::~Weak() {
    reset();
}

template <typename T>
Local<T> Weak<T>::get() const {
    return Local<T>{impl.ref_.Get(impl.engine_->isolate_)};
}

template <typename T>
Local<Value> Weak<T>::getValue() const {
    return Local<Value>{impl.ref_.Get(impl.engine_->isolate_).template As<v8::Value>()};
}

template <typename T>
bool Weak<T>::isEmpty() const {
    return impl.ref_.IsEmpty();
}

template <typename T>
void Weak<T>::reset() {
    impl.ref_.Reset();
    impl.engine_ = nullptr;
}
template <typename T>
void Weak<T>::reset(Local<T> const& val) {
    auto raw = ValueHelper::unwrap(val);
    if (raw.IsEmpty()) {
        reset();
        return;
    }
    auto& current = EngineScope::currentEngineChecked();
    impl.ref_.Reset(current.isolate_, raw);
    impl.engine_ = &current;
    impl.markWeak();
}
template <typename T>
Engine* Weak<T>::engine() const {
    return impl.engine_;
}


} // namespace jspp