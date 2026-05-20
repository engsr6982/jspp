#pragma once
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"

#include <stdexcept>
#include <type_traits>
#include <typeinfo>

namespace jspp::binding::traits {

namespace internal {

template <typename T>
struct PolymorphicTypeHookBase {
    static const void* get(const T* src, const std::type_info*& type) {
        type = src ? &typeid(T) : nullptr;
        return src;
    }
};

// Specialization: If it is a polymorphic type, use dynamic_cast<void*>
// to obtain the starting address of the most derived object
template <typename T>
    requires std::is_polymorphic_v<T>
struct PolymorphicTypeHookBase<T> {
    static const void* get(const T* src, const std::type_info*& type) {
        if (src) {
            type = &typeid(*src);                  // RTTI Get Real Type
            return dynamic_cast<const void*>(src); // Get the most top-level address (Most Derived Address)
        }
        type = nullptr;
        return nullptr;
    }
};

} // namespace internal


template <typename T>
struct PolymorphicTypeHook : internal::PolymorphicTypeHookBase<T> {
    // static const void* get(const T* src, const std::type_info*& type) {}
};


namespace detail {

struct ResolvedCastSource {
    void const*      ptr;           // The C++ pointer ultimately chosen (possibly offset)
    ClassMeta const* meta;          // The JS class definition ultimately chosen
    bool             is_downcasted; // Whether downcasting polymorphism is successful
};

template <typename T>
    requires(!std::is_void_v<T>)
ResolvedCastSource resolveCastSource(T* value) {
    auto& engine = EngineScope::currentEngineChecked();

    // 1. Obtain dynamic type and base address
    const std::type_info* dynamicType = nullptr;
    const void*           dynamicPtr  = traits::PolymorphicTypeHook<T>::get(value, dynamicType);

    // 2. Try to resolve dynamic type (Downcast)
    if (dynamicType && dynamicPtr) {
        if (auto* meta = engine.getClassMeta(std::type_index(*dynamicType))) {
            return {dynamicPtr, meta, true}; // Perfectly hit subclass
        }
    }

    // 3. Fallback to static type (Original)
    std::type_index staticIdx(typeid(T));
    auto*           staticMeta = engine.getClassMeta(staticIdx);
    if (!staticMeta) [[unlikely]] {
        throw std::logic_error("Class not registered: " + std::string(staticIdx.name()));
    }

    return {static_cast<const void*>(value), staticMeta, false};
}

} // namespace detail

} // namespace jspp::binding::traits