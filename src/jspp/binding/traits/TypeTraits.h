#pragma once
#include <type_traits>

#include <string_view>

#include "jspp/core/Concepts.h"

namespace jspp::binding::traits {

template <typename T>
struct RawTypeHelper {
    using Decayed  = std::decay_t<T>;
    using Stripped = std::remove_pointer_t<Decayed>;

    using type = std::conditional_t<
        concepts::StringLike<Decayed>,
        std::string_view,             // StringLike<T> forcibly mapped to string_view (fix const char[N])
        std::remove_const_t<Stripped> // Other types: remove pointers and modifiers
        >;
};

template <typename T>
using RawType_t = typename RawTypeHelper<T>::type;


/// A nullary callable that supplies the instance for provider-backed static bindings.
template <typename P>
concept InstanceProvider = requires(P& provider) { std::invoke(provider); };

/// An InstanceProvider that returns an lvalue reference (T& or T const&).
template <typename P>
concept ReferenceInstanceProvider = InstanceProvider<P> && requires(P& provider) {
    requires std::is_lvalue_reference_v<decltype(std::invoke(provider))>;
};

/// An InstanceProvider that returns a pointer (T* or T const*).
template <typename P>
concept PointerInstanceProvider = InstanceProvider<P> && requires(P& provider) {
    requires std::is_pointer_v<std::remove_cvref_t<decltype(std::invoke(provider))>>;
};

/// An InstanceProvider that borrows the instance: the returned object remains owned by the host.
template <typename P>
concept BorrowingInstanceProvider = ReferenceInstanceProvider<P> || PointerInstanceProvider<P>;

/**
 * @brief Normalized return form of a borrowing InstanceProvider.
 * @note `Ref` is always an lvalue reference, so it can be compared against the member function's
 *       class with std::is_convertible_v, covering const qualification and derived-to-base
 *       upcasting.
 */
template <typename P>
    requires(BorrowingInstanceProvider<P>)
struct InstanceProviderTraits {
private:
    using Unref = std::remove_reference_t<std::invoke_result_t<P&>>;
    using Bare  = std::remove_cv_t<Unref>;

public:
    using Pointee = std::conditional_t<PointerInstanceProvider<P>, std::remove_pointer_t<Bare>, Unref>;
    /// Provided instance type with const removed.
    using Element = std::remove_cv_t<Pointee>;
    /// Whether the provider exposes a const instance.
    static constexpr bool is_const = std::is_const_v<Pointee>;
    /// Normalized instance reference type: T& or T const&.
    using Ref = std::conditional_t<is_const, Element const&, Element&>;
};

template <typename T>
inline constexpr size_t size_of_v = sizeof(T);

template <>
inline constexpr size_t size_of_v<void> = 0;


template <typename T>
inline constexpr bool isFunctionCallback_v = std::convertible_to<std::remove_cvref_t<T>, FunctionCallback>
                                          || std::is_invocable_r_v<Local<Value>, T, Arguments const&>;

template <typename T>
inline constexpr bool isGetterCallback_v =
    std::convertible_to<std::remove_cvref_t<T>, GetterCallback> || std::is_invocable_r_v<Local<Value>, T>;

template <typename T>
inline constexpr bool isSetterCallback_v =
    std::convertible_to<std::remove_cvref_t<T>, SetterCallback> || std::is_invocable_r_v<void, T, Local<Value> const&>;

template <typename T>
inline constexpr bool isInstanceMethodCallback_v =
    std::convertible_to<std::remove_cvref_t<T>, InstanceMethodCallback>
    || std::is_invocable_r_v<Local<Value>, T, InstancePayload&, Arguments const&>;

template <typename T>
inline constexpr bool isInstanceGetterCallback_v =
    std::convertible_to<std::remove_cvref_t<T>, InstanceGetterCallback>
    || std::is_invocable_r_v<Local<Value>, T, InstancePayload&, Arguments const&>;

template <typename T>
inline constexpr bool isInstanceSetterCallback_v = std::convertible_to<std::remove_cvref_t<T>, InstanceSetterCallback>
                                                || std::is_invocable_r_v<void, T, InstancePayload&, Arguments const&>;


// unique_ptr
template <typename T>
struct is_unique_ptr : std::false_type {};
template <typename T, typename D>
struct is_unique_ptr<std::unique_ptr<T, D>> : std::true_type {};

template <typename T>
inline constexpr bool is_unique_ptr_v = is_unique_ptr<std::remove_cvref_t<T>>::value;

// shared_ptr
template <typename T>
struct is_shared_ptr : std::false_type {};
template <typename T>
struct is_shared_ptr<std::shared_ptr<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_shared_ptr_v = is_shared_ptr<std::remove_cvref_t<T>>::value;

// weak_ptr
template <typename T>
struct is_weak_ptr : std::false_type {};
template <typename T>
struct is_weak_ptr<std::weak_ptr<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_weak_ptr_v = is_weak_ptr<std::remove_cvref_t<T>>::value;

template <typename T>
inline constexpr bool is_std_smart_pointer = is_unique_ptr_v<T> || is_shared_ptr_v<T> || is_weak_ptr_v<T>;

} // namespace jspp::binding::traits