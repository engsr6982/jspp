#pragma once
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
    using ArgsTuple                   = std::tuple<Args...>;
    static constexpr size_t ArgsCount = sizeof...(Args);
    static constexpr bool   isConst   = false;
};

template <typename C, typename R, typename... Args>
struct FunctionTraits<R (C::*)(Args...) const> {
    using ReturnType                  = R;
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


} // namespace jspp::binding::traits