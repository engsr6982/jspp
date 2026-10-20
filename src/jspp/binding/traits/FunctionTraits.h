#pragma once
#include "TypeTraits.h"

#include <functional>


namespace jspp::binding::traits {


template <typename T, typename = void>
struct FunctionTraits {};

// function / function pointer
template <typename R, typename... Args>
struct FunctionTraits<R (*)(Args...)> {
    using ReturnType                  = R;
    using ArgsTuple                   = std::tuple<Args...>;
    static constexpr size_t ArgsCount = sizeof...(Args);
    static constexpr bool   isConst   = false;
};

template <typename R, typename... Args>
struct FunctionTraits<R(Args...)> : FunctionTraits<R (*)(Args...)> {};

// C++17 noexcept
template <typename R, typename... Args>
struct FunctionTraits<R (*)(Args...) noexcept> : FunctionTraits<R (*)(Args...)> {};

template <typename R, typename... Args>
struct FunctionTraits<R(Args...) noexcept> : FunctionTraits<R (*)(Args...)> {};

// std::function
template <typename R, typename... Args>
struct FunctionTraits<std::function<R(Args...)>> : FunctionTraits<R (*)(Args...)> {};

// member function pointer
template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...)> {
    using ReturnType                  = R;
    using ClassType                   = C;
    using ArgsTuple                   = std::tuple<Args...>;
    static constexpr size_t ArgsCount = sizeof...(Args);
    static constexpr bool   isConst   = false;
};

template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...) const> {
    using ReturnType                  = R;
    using ClassType                   = C;
    using ArgsTuple                   = std::tuple<Args...>;
    static constexpr size_t ArgsCount = sizeof...(Args);
    static constexpr bool   isConst   = true;
};

// C++17 noexcept member function pointer
template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...) noexcept> : FunctionTraits<R (C::*)(Args...)> {};

template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...) const noexcept> : FunctionTraits<R (C::*)(Args...) const> {};

// Functor & Lambda
template <typename T>
struct FunctionTraits<T, std::void_t<decltype(&T::operator())>> : FunctionTraits<decltype(&T::operator())> {};


template <typename T>
concept Callable = requires { typename FunctionTraits<std::remove_cvref_t<T>>::ReturnType; };

template <Callable T>
constexpr size_t ArgsCount_v = FunctionTraits<std::remove_cvref_t<T>>::ArgsCount;

template <Callable T, size_t N>
using ArgumentType_t = std::tuple_element_t<N, typename FunctionTraits<std::remove_cvref_t<T>>::ArgsTuple>;


/**
 * @brief Whether Provider can supply the instance required by a bound member function.
 * @tparam Method A member function pointer (or a reference to one), e.g. decltype(&T::foo).
 * @note Checks that the provider borrows its result, Method is a member function pointer, and the
 *       provider's instance type is implicitly convertible to Method's class (respecting const
 *       qualification and derived-to-base upcasting).
 */
template <typename Provider, typename Method>
concept InstanceProviderFor = requires {
    requires BorrowingInstanceProvider<Provider>;
    requires std::is_member_function_pointer_v<std::remove_cvref_t<Method>>;
    requires std::is_convertible_v<
        typename InstanceProviderTraits<Provider>::Ref,
        std::conditional_t<
            FunctionTraits<std::remove_cvref_t<Method>>::isConst,
            typename FunctionTraits<std::remove_cvref_t<Method>>::ClassType const&,
            typename FunctionTraits<std::remove_cvref_t<Method>>::ClassType&>>;
};


} // namespace jspp::binding::traits