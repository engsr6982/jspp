#pragma once
#include "NativeInstanceImpl.h"
#include "ReturnValuePolicy.h"
#include "traits/FunctionTraits.h"
#include "traits/Polymorphic.h"
#include "traits/TypeTraits.h"
#include "v8kit/core/Exception.h"
#include "v8kit/core/InstancePayload.h"
#include "v8kit/core/MetaInfo.h"
#include "v8kit/core/Reference.h"
#include "v8kit/core/TrackedHandle.h"
#include "v8kit/core/Value.h"

#include <cassert>
#include <stdexcept>
#include <string>
#include <variant>

namespace v8kit::binding {

namespace detail {

template <typename T>
struct GenericTypeConverter;

} // namespace detail

/**
 * @brief 类型转换器
 * @tparam T C++ RawType (class Foo const& -> Foo -> TypeConverter<Foo>::toJs/toCpp)
 *
 * @example TypeConverter<Foo>::toJs(Foo* / Foo& / Foo const& / Foo value) -> Local<T>
 * @example TypeConverter<Foo>::toCpp(Local<Value> const& value) -> T* / T& / T
 */
template <typename T>
struct TypeConverter;

template <typename T>
using RawTypeConverter = TypeConverter<traits::RawType_t<T>>;

template <typename T>
concept HasTypeConverter = requires { typename RawTypeConverter<T>; };
template <typename T>
inline constexpr bool HasTypeConverter_v = HasTypeConverter<T>;

namespace internal {

/**
 * @brief C++ 值类型转换器
 * @note 此转换器设计目的是对于某些特殊情况，例如 void foo(std::string_view)
 *       在绑定时，TypeConverter 对字符串的特化是接受 StringLike，但返回值统一为 std::string
 *       这种特殊情况下，会导致 toCpp<std::string_view> 内部类型断言失败:
 * @code using RawConvRet = std::remove_cv_t<std::remove_reference_t<TypedToCppRet<std::string_view>>> // std::string
 * @code std::same_v<RawConvRet, std::string_view> // false
 *
 * @note 为了解决此问题，引入了 CppValueTypeTransformer，用于放宽类型约束
 * @note 需要注意的是 CppValueTypeTransformer 仅放宽了类型约束，实际依然需要特化 TypeConverter<T>
 */
template <typename Form, typename To>
struct CppValueTypeTransformer : std::false_type {};

template <>
struct CppValueTypeTransformer<std::string, std::string_view> : std::true_type {};

template <typename From, typename To>
inline constexpr bool CppValueTypeTransformer_v = CppValueTypeTransformer<From, To>::value;

} // namespace internal

/**
 * Convert C++ type to js type
 * @tparam T C++ type
 * @param val C++ value
 * @return Local<Value>
 * @note forward to RawTypeConverter<T>::toJs(T)
 */
template <typename T>
[[nodiscard]] Local<Value> toJs(T&& val);

/**
 * Convert js type to C++ type
 * @tparam T C++ type
 * @param val C++ value
 * @param policy Return value policy
 * @param parent Parent object
 * @return Local<Value>
 * @note if not RawTypeConverter<T>::toJs(T, policy, parent) is defined, forward to RawTypeConverter<T>::toJs(T)
 */
template <typename T>
[[nodiscard]] Local<Value> toJs(T&& val, ReturnValuePolicy policy, Local<Value> parent);

template <typename T>
[[nodiscard]] decltype(auto) toCpp(Local<Value> const& value);


// -----------------------------
// impl start
// -----------------------------

namespace detail {


template <typename T>
struct GenericTypeConverter {
    template <typename U>
    static ReturnValuePolicy handleAutomaticPolicy(ReturnValuePolicy policy) {
        return detail::resolveAutomaticPolicy<U>(policy);
    }

    // C++ -> JS
    // U is introduced to enable perfect forwarding.
    // T cannot be used because it is already determined by the enclosing template,
    // so T&& would be a pure rvalue reference rather than a forwarding reference.
    template <typename U>
    static Local<Value> toJs(U&& value, ReturnValuePolicy policy, Local<Value> parent) {
        policy = handleAutomaticPolicy<U>(policy);

        using ElementType   = typename traits::detail::ElementTypeExtractor<U>::type;
        ElementType* rawPtr = nullptr;

        // 裸指针/智能指针判定时，需要剥离引用的原始类型(保留 const 语义)
        using BaseU = std::remove_reference_t<U>;

        if constexpr (std::is_pointer_v<BaseU>) {
            rawPtr = value;
            if (!rawPtr) return Null::newNull();
        } else if constexpr (traits::is_unique_ptr_v<BaseU> || traits::is_shared_ptr_v<BaseU>) {
            rawPtr = value.get();
            if (!rawPtr) return Null::newNull();
        } else {
            rawPtr = &value;
        }

        // 查表：解析对象的最终多态 Meta 和首地址偏移
        auto resolved = traits::detail::resolveCastSource<ElementType>(rawPtr);

        // 创建包装着 C++ 实例的底座 (NativeInstance)
        auto instance = factory::createNativeInstance(std::forward<U>(value), policy, resolved);
        if (!instance) return Null::newNull();

        auto&         engine = EngineScope::currentEngineChecked();
        Local<Object> jsObj  = engine.newInstance(*resolved.meta, std::move(instance));

        if (policy == ReturnValuePolicy::kReferenceInternal
            || policy == ReturnValuePolicy::kReferenceInternalPersistent) {
            if (!parent.isObject()) {
                throw Exception("kReferenceInternal/kReferenceInternalPersistent requires a valid parent object");
            }
            if (!engine.trySetReferenceInternal(parent.asObject(), jsObj)) {
                throw Exception("Failed to set reference internal");
            }
        }
        return jsObj;
    }

    // JS -> C++
    static T* toCpp(Local<Value> const& value) {
        auto& engine  = EngineScope::currentEngineChecked();
        auto  payload = engine.getInstancePayload(value.asObject());
        if (!payload) {
            throw Exception("Argument is not a native instance");
        }
        auto ptr = payload->getHolder()->unwrap<T>();
        if (!ptr) throw Exception("Type mismatch or cast failed");
        return ptr;
    }
};

} // namespace detail

template <typename T>
struct TypeConverter : detail::GenericTypeConverter<T> {};

// v8kit::Local<T>
template <typename T>
    requires concepts::WrapType<T>
struct TypeConverter<Local<T>> {
    static Local<Value> toJs(Local<T> const& value) { return value.asValue(); }
    static Local<T>     toCpp(Local<Value> const& value) { return value.as<T>(); }
};

// bool <-> Boolean
template <>
struct TypeConverter<bool> {
    static Local<Boolean> toJs(bool value) { return Boolean::newBoolean(value); }
    static bool           toCpp(Local<Value> const& value) { return value.asBoolean().getValue(); }
};

// int/uint/float/double/int64/uint64 <-> Number/BigInt
template <typename T>
    requires concepts::NumberLike<T>
struct TypeConverter<T> {
    static Local<Value> toJs(T value) {
        if constexpr (std::same_as<T, int64_t> || std::same_as<T, uint64_t>) {
            return BigInt::newBigInt(value); // C++ -> Js: 严格类型转换
        } else {
            return Number::newNumber(value);
        }
    }
    static T toCpp(Local<Value> const& value) {
        if (value.isNumber()) {
            return value.asNumber().getValueAs<T>(); // Js -> C++: 宽松转换
        }
        if (value.isBigInt()) {
            if constexpr (std::same_as<T, int64_t>) {
                return value.asBigInt().getInt64();
            } else {
                return value.asBigInt().getUint64();
            }
        }
        [[unlikely]] throw Exception{"Cannot convert value to NumberLike<T>", Exception::Type::TypeError};
    }
};

// std::string <-> String
template <typename T>
    requires concepts::StringLike<T>
struct TypeConverter<T> {
    static Local<String> toJs(T const& value) { return String::newString(std::string_view{value}); }
    static std::string   toCpp(Local<Value> const& value) { return value.asString().getValue(); } // always UTF-8
};

// enum -> Number (enum value)
template <typename T>
    requires std::is_enum_v<T>
struct TypeConverter<T> {
    static Local<Number> toJs(T value) { return Number::newNumber(static_cast<int>(value)); }
    static T             toCpp(Local<Value> const& value) { return static_cast<T>(value.asNumber().getInt32()); }
};

// std::optional <-> null/undefined
template <typename T>
struct TypeConverter<std::optional<T>> {
    static Local<Value> toJs(std::optional<T> const& value) {
        if (value) {
            return binding::toJs(value.value());
        }
        return Null::newNull(); // default to null
    }
    static std::optional<T> toCpp(Local<Value> const& value) {
        if (value.isNullOrUndefined()) {
            return std::nullopt;
        }
        return std::optional<T>{binding::toCpp<T>(value)};
    }
};

// std::vector <-> Array
template <typename T>
struct TypeConverter<std::vector<T>> {
    static Local<Value> toJs(std::vector<T> const& value) {
        auto array = Array::newArray(value.size());
        for (std::size_t i = 0; i < value.size(); ++i) {
            array.set(i, binding::toJs(value[i]));
        }
        return array;
    }
    static std::vector<T> toCpp(Local<Value> const& value) {
        auto array = value.asArray();

        std::vector<T> result;
        result.reserve(array.length());
        for (std::size_t i = 0; i < array.length(); ++i) {
            result.push_back(binding::toCpp<T>(array[i]));
        }
        return result;
    }
};

template <typename K, typename V>
    requires concepts::StringLike<K> // JavaScript only supports string keys
struct TypeConverter<std::unordered_map<K, V>> {
    static_assert(HasTypeConverter_v<V>, "Cannot convert std::unordered_map to Object; type V has no TypeConverter");

    static Local<Value> toJs(std::unordered_map<K, V> const& value) {
        auto object = Object::newObject();
        for (auto const& [key, val] : value) {
            object.set(String::newString(key), binding::toJs(val));
        }
        return object;
    }

    static std::unordered_map<K, V> toCpp(Local<Value> const& value) {
        auto object = value.asObject();
        auto keys   = object.getOwnPropertyNames();

        std::unordered_map<K, V> result;
        for (auto const& key : keys) {
            result[key.getValue()] = binding::toCpp<V>(object.get(key));
        }
        return result;
    }
};

// std::variant <-> Type
template <typename... Is>
struct TypeConverter<std::variant<Is...>> {
    static_assert(
        (HasTypeConverter_v<Is> && ...),
        "Cannot convert std::variant to Object; all types must have a TypeConverter"
    );
    using TypedVariant = std::variant<Is...>;

    static Local<Value> toJs(TypedVariant const& value) {
        if (value.valueless_by_exception()) {
            return Null::newNull();
        }
        return std::visit([&](auto const& v) -> Local<Value> { return binding::toJs(v); }, value);
    }

    static TypedVariant toCpp(Local<Value> const& value) { return tryToCpp(value); }

    template <size_t I = 0>
    static TypedVariant tryToCpp(Local<Value> const& value) {
        if constexpr (I >= sizeof...(Is)) {
            throw Exception{
                "Cannot convert Value to std::variant; no matching type found.",
                Exception::Type::TypeError
            };
        } else {
            using Type = std::variant_alternative_t<I, TypedVariant>;
            try {
                return binding::toCpp<Type>(value);
            } catch (Exception const&) {
                return tryToCpp<I + 1>(value);
            }
        }
    }
};

// std::monostate <-> null/undefined
template <>
struct TypeConverter<std::monostate> {
    static Local<Value> toJs(std::monostate) { return Null::newNull(); }

    static std::monostate toCpp(Local<Value> const& value) {
        if (value.isNullOrUndefined()) {
            return std::monostate{};
        }
        [[unlikely]] throw Exception{"Expected null/undefined for std::monostate", Exception::Type::TypeError};
    }
};

// std::pair <-> [T1, T2]
template <typename Ty1, typename Ty2>
struct TypeConverter<std::pair<Ty1, Ty2>> {
    static_assert(HasTypeConverter_v<Ty1>);
    static_assert(HasTypeConverter_v<Ty2>);

    static Local<Value> toJs(std::pair<Ty1, Ty2> const& pair) {
        auto array = Array::newArray(2);
        array.set(0, binding::toJs(pair.first));
        array.set(1, binding::toJs(pair.second));
        return array;
    }
    static std::pair<Ty1, Ty2> toCpp(Local<Value> const& value) {
        if (!value.isArray() || value.asArray().length() != 2) {
            throw Exception{"Invalid argument type, expected array with 2 elements"};
        }
        auto array = value.asArray();
        return std::make_pair(binding::toCpp<Ty1>(array.get(0)), binding::toCpp<Ty2>(array.get(1)));
    }
};


// fwd decl
namespace adapter {
template <typename R, typename... Args>
std::function<R(Args...)> wrapScriptCallback(Local<Value> const& value);
template <typename Fn>
FunctionCallback wrapFunction(Fn&& fn, ReturnValuePolicy policy);
} // namespace adapter

// std::function -> Function
template <typename R, typename... Args>
struct TypeConverter<std::function<R(Args...)>> {
    static_assert(
        (HasTypeConverter_v<Args> && ...),
        "Cannot convert std::function to Function; all parameter types must have a TypeConverter"
    );
    using Fn = std::function<R(Args...)>;

    // static Local<Value> toJs(Fn const& value, ReturnValuePolicy policy, Local<Value> /*parent*/) {
    //     return adapter::wrapFunction(std::forward<Fn>(value), policy);
    // }
    static Fn toCpp(Local<Value> const& value) { return adapter::wrapScriptCallback<R, Args...>(value); }
};

// TODO: support smart pointer
// template <typename T>
// struct TypeConverter<std::shared_ptr<T>> {};


// free functions
template <typename T>
Local<Value> toJs(T&& val) {
    return RawTypeConverter<T>::toJs(std::forward<T>(val));
}

template <typename T>
Local<Value> toJs(T&& val, ReturnValuePolicy policy, Local<Value> parent) {
    if constexpr (requires { RawTypeConverter<T>::toJs(std::forward<T>(val), policy, parent); }) {
        return RawTypeConverter<T>::toJs(std::forward<T>(val), policy, parent);
    } else {
        if constexpr (!requires { RawTypeConverter<T>::toJs(std::forward<T>(val)); }) {
            static_assert(sizeof(T) == 0, "No suitable toJs converter found for this type.");
        }
        return toJs(std::forward<T>(val)); // try drop policy and parent
    }
}

template <typename T>
decltype(auto) toCpp(Local<Value> const& value) {
    using BareT = std::remove_cv_t<std::remove_reference_t<T>>; // T

    using Conv    = RawTypeConverter<T>;
    using ConvRet = decltype(Conv::toCpp(std::declval<Local<Value>>()));

    if constexpr (std::is_lvalue_reference_v<T>) {
        // 左值引用 T&
        if constexpr (std::is_pointer_v<std::remove_reference_t<ConvRet>>) {
            auto p = Conv::toCpp(value); // 返回 T*
            if (p == nullptr) [[unlikely]] {
                throw std::runtime_error("TypeConverter::toCpp returned a null pointer.");
            }
            return static_cast<T&>(*p); // 返回 T&
        } else if constexpr (std::is_lvalue_reference_v<ConvRet> || std::is_const_v<std::remove_reference_t<T>>) {
            return Conv::toCpp(value); // 已返回 T&，直接转发 或者 const T& 可以绑定临时
        } else {
            static_assert(
                std::is_pointer_v<std::remove_reference_t<ConvRet>> || std::is_lvalue_reference_v<ConvRet>,
                "TypeConverter::toCpp must return either T* or T& when toCpp<T&> is required. Returning T (by "
                "value) cannot bind to a non-const lvalue reference; change TypeConverter or request a value type."
            );
        }
    } else if constexpr (std::is_pointer_v<T>) {
        // 指针类型 T*
        if constexpr (std::is_pointer_v<std::remove_reference_t<ConvRet>>) {
            return Conv::toCpp(value); // 直接返回
        } else if constexpr (std::is_lvalue_reference_v<ConvRet>) {
            return std::addressof(Conv::toCpp(value)); // 返回 T& -> 可以取地址
        } else {
            static_assert(
                std::is_pointer_v<std::remove_reference_t<ConvRet>> || std::is_lvalue_reference_v<ConvRet>,
                "TypeConverter::toCpp must return T* or T& when toCpp<T*> is required. "
                "Returning T (by value) would produce pointer to temporary (unsafe)."
            );
        }
    } else {
        // 值类型 T
        using RawConvRet = std::remove_cv_t<std::remove_reference_t<ConvRet>>;
        if constexpr ((std::is_same_v<RawConvRet, BareT> || internal::CppValueTypeTransformer_v<RawConvRet, BareT>)
                      && !std::is_pointer_v<std::remove_reference_t<ConvRet>> && !std::is_lvalue_reference_v<ConvRet>) {
            return Conv::toCpp(value); // 按值返回 / 直接返回 (可能 NRVO)
        } else {
            static_assert(
                std::is_same_v<RawConvRet, BareT> && !std::is_pointer_v<std::remove_reference_t<ConvRet>>
                    && !std::is_lvalue_reference_v<ConvRet>,
                "TypeConverter::toCpp must return T (by value) for toCpp<T>. "
                "Other return forms (T* or T&) are not supported for value request."
            );
        }
    }
}


namespace adapter {

template <typename TargetT>
using ConverterRetType = decltype(toCpp<TargetT>(std::declval<Local<Value>>()));

template <typename TargetT>
struct StorageTypeDetector {
    using RetT = ConverterRetType<TargetT>;

    // 核心逻辑：
    // 1. 如果 Converter 返回左值引用 (Foo&)，说明对象已存在 -> Tuple 存 Foo&
    // 2. 如果 Converter 返回右值/值 (std::string, int)，说明是临时对象 -> Tuple 存 std::string (按值存储以保活)
    using type = std::conditional_t<
        std::is_lvalue_reference_v<RetT>,
        RetT,                     // Keep Ref
        std::remove_cvref_t<RetT> // Decay to Value (remove const/volatile/ref)
        >;
};

template <typename TargetT>
using StorageType_t = StorageTypeDetector<TargetT>::type;

template <typename Tuple, std::size_t... Is>
inline decltype(auto) ConvertArgsToTuple(Arguments const& args, std::index_sequence<Is...>) {
    using SafeTuple = std::tuple<StorageType_t<std::tuple_element_t<Is, Tuple>>...>;
    return SafeTuple{toCpp<std::tuple_element_t<Is, Tuple>>(args[Is])...};
}


// ---------------------
// Adapter impl
// ---------------------

// JavaScript lambda -> std::function
template <typename R, typename... Args>
std::function<R(Args...)> wrapScriptCallback(Local<Value> const& value) {
    if (!value.isFunction()) [[unlikely]] {
        throw Exception("expected function", Exception::Type::TypeError);
    }
    auto& engine = EngineScope::currentEngineChecked();

    // 使用跟踪句柄，避免 C++ 侧拷贝 Lambda、长期持有 Global 导致 v8::Isolate 析构异常
    auto safeKeep = TrackedGlobal<Function>::create(value.asFunction());

    return [keep = std::move(safeKeep), engine = &engine](Args&&... args) -> R {
        EngineScope lock{engine};

        auto& global = keep->global();
        if (global.isEmpty()) {
            if constexpr (std::is_void_v<R>) {
                return;
            } else {
                // 当跟踪句柄失效，代表引擎可能已销毁，这里抛运行时异常
                throw std::runtime_error{"Engine already destroyed"};
            }
        }

        TransientObjectScope enter{}; // 激活瞬时作用域，避免 JS 闭包逃逸 导致UAF

        std::array<Local<Value>, sizeof...(Args)> argv{
            toJs(std::forward<Args>(args), ReturnValuePolicy::kReference, Local<Value>{})...
        };
        if constexpr (std::is_void_v<R>) {
            global.get().call({}, argv);
            return;
        } else {
            return toCpp<R>(global.get().call({}, argv));
        }
    };
}

// C++ function -> JavaScript function
template <typename Fn>
FunctionCallback wrapFunction(Fn&& fn, ReturnValuePolicy policy) {
    if constexpr (traits::isFunctionCallback_v<Fn>) {
        return std::forward<Fn>(fn);
    }
    return [f = std::forward<Fn>(fn), policy](Arguments const& args) -> Local<Value> {
        using Trait = traits::FunctionTraits<std::decay_t<Fn>>;
        using R     = typename Trait::ReturnType;
        using Tuple = typename Trait::ArgsTuple;

        constexpr auto Count = Trait::ArgsCount;
        if (args.length() != Count) [[unlikely]] {
            throw Exception("argument count mismatch", Exception ::Type::TypeError);
        }

        if constexpr (std::is_void_v<R>) {
            std::apply(f, ConvertArgsToTuple<Tuple>(args, std::make_index_sequence<Count>()));
            return {}; // undefined
        } else {
            decltype(auto) ret = std::apply(f, ConvertArgsToTuple<Tuple>(args, std::make_index_sequence<Count>()));
            return toJs(ret, policy, args.hasThiz() ? args.thiz() : Local<Value>{});
        }
    };
}


template <typename R, typename C, size_t Len, typename... Args>
R dispatchOverloadImpl(std::array<C, Len> const& overloads, Args&&... args) {
    // TODO: consider optimizing overload dispatch (e.g. arg-count lookup)
    // if we ever hit cases with >3 overloads. Current linear dispatch is ideal
    // for small sets and keeps the common path fast.
    for (size_t i = 0; i < Len; ++i) {
        try {
            return std::invoke(overloads[i], std::forward<Args>(args)...);
        } catch (Exception const&) {
            if (i == Len - 1) [[unlikely]] {
                throw Exception{"no overload found", Exception::Type::TypeError};
            }
        }
    }
    return R{};
}

template <size_t Len>
inline FunctionCallback _mergeFunctionCallbacks(std::array<FunctionCallback, Len> overloads) {
    return [fs = std::move(overloads)](Arguments const& args) -> Local<Value> {
        return dispatchOverloadImpl<Local<Value>>(fs, args);
    };
}

template <typename... Overload>
FunctionCallback wrapOverloadFunction(ReturnValuePolicy policy, Overload&&... fn) {
    std::array<FunctionCallback, sizeof...(Overload)> overloads = {wrapFunction(std::forward<Overload>(fn), policy)...};
    return _mergeFunctionCallbacks(std::move(overloads));
}

template <typename... Overload>
FunctionCallback wrapOverloadFuncAndExtraPolicy(Overload&&... fn) {
    constexpr size_t policy_count = (static_cast<size_t>(traits::is_policy<Overload>::value) + ...);
    static_assert(policy_count <= 1, "ReturnValuePolicy can only appear once in argument list");

    ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic;
    if constexpr (policy_count > 0) {
        (
            [&](auto&& arg) {
                if constexpr (traits::is_policy<decltype(arg)>::value) {
                    policy = arg;
                }
            }(fn),
            ...
        );
    }

    constexpr size_t func_count = sizeof...(Overload) - policy_count;
    static_assert(func_count > 0, "No functions provided to overload");

    auto overloads = [&]() {
        std::array<FunctionCallback, func_count> arr;
        size_t                                   idx = 0;
        (
            [&](auto&& arg) {
                if constexpr (!traits::is_policy<decltype(arg)>::value) {
                    arr[idx++] = wrapFunction(std::forward<decltype(arg)>(arg), policy);
                }
            }(std::forward<Overload>(fn)),
            ...
        );
        return arr;
    }();

    return _mergeFunctionCallbacks(std::move(overloads));
}


// C++ Getter / Setter -> JavaScript Getter / Setter
template <typename Fn>
GetterCallback wrapGetter(Fn&& getter, ReturnValuePolicy policy) {
    if constexpr (traits::isGetterCallback_v<Fn>) {
        return std::forward<Fn>(getter);
    }
    return [get = std::forward<Fn>(getter), policy]() -> Local<Value> {
        using Trait = traits::FunctionTraits<std::decay_t<Fn>>;
        using R     = Trait::ReturnType;
        static_assert(!std::is_void_v<R>, "Getter must return a value");
        static_assert(Trait::ArgsCount == 0, "Getter must not take arguments");

        decltype(auto) value = std::invoke(get);
        return toJs(value, policy, {});
    };
}
template <typename Fn>
SetterCallback wrapSetter(Fn&& setter) {
    if constexpr (traits::isSetterCallback_v<Fn>) {
        return std::forward<Fn>(setter);
    }
    return [set = std::forward<Fn>(setter)](Local<Value> const& value) -> void {
        using Trait = traits::FunctionTraits<std::decay_t<Fn>>;
        using R     = Trait::ReturnType;
        static_assert(std::is_void_v<R>, "Setter must not return a value");
        static_assert(Trait::ArgsCount == 1, "Setter must take one argument");

        using Args = Trait::ArgsTuple;
        using Type = std::tuple_element_t<0, Args>;
        std::invoke(set, toCpp<Type>(value));
    };
}

template <typename Ty, bool forceReadonly = false>
std::pair<GetterCallback, SetterCallback> wrapStaticMember(Ty&& member, ReturnValuePolicy policy) {
    static_assert(!std::is_member_pointer_v<std::remove_cvref_t<Ty>>);

    using RawType = std::remove_reference_t<Ty>;
    if constexpr (std::is_pointer_v<RawType>) {
        // Ty* / Ty const*
        using ValueType = std::remove_pointer_t<RawType>;

        GetterCallback getter = [member, policy]() -> Local<Value> {
            if (!member) throw Exception("Accessing null static member pointer");
            return toJs(*member, policy, {});
        };
        SetterCallback setter = nullptr;
        if constexpr (!std::is_const_v<ValueType> && !forceReadonly) {
            setter = [member](Local<Value> const& val) {
                if (!member) throw Exception("Accessing null static member pointer");
                *member = toCpp<ValueType>(val);
            };
        }
        return {std::move(getter), std::move(setter)};
    } else {
        // Ty
        GetterCallback getter = [val = std::forward<Ty>(member), policy]() -> Local<Value> {
            // 对常量的 toJs，policy 通常是 Copy (对于基础类型)
            // 如果是大对象，policy 可能是 Reference，但引用的将是 lambda 内部的 val
            return toJs(val, policy, {});
        };
        return {std::move(getter), nullptr};
    }
}


template <typename C, typename... Args>
ConstructorCallback wrapConstructor() {
    return [](Arguments const& args) -> std::unique_ptr<NativeInstance> {
        constexpr size_t N = sizeof...(Args);
        if constexpr (N == 0) {
            static_assert(
                concepts::HasDefaultConstructor<C>,
                "Class C must have a no-argument constructor; otherwise, a constructor must be specified."
            );
            if (args.length() != 0) return nullptr; // Parameter mismatch
            return factory::newNativeInstance<C>();

        } else {
            if (args.length() != N) return nullptr; // Parameter mismatch

            using Tuple = std::tuple<Args...>;

            auto parameters = ConvertArgsToTuple<Tuple>(args, std::make_index_sequence<N>());
            return std::apply(
                [](auto&&... unpackedArgs) {
                    return factory::newNativeInstance<C>(std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
                },
                std::move(parameters)
            );
        }
    };
}

template <typename C, typename Fn>
InstanceMethodCallback wrapInstanceMethod(Fn&& fn, ReturnValuePolicy policy) {
    if constexpr (traits::isInstanceMethodCallback_v<Fn>) {
        return std::forward<Fn>(fn); // 已是标准的回调，直接转发不需要进行绑定
    }
    return [f = std::forward<Fn>(fn), policy](InstancePayload& payload, const Arguments& args) -> Local<Value> {
        using Trait = traits::FunctionTraits<std::decay_t<Fn>>;
        using R     = typename Trait::ReturnType;
        using Tuple = typename Trait::ArgsTuple;

        constexpr size_t ArgsCount = Trait::ArgsCount;
        if (args.length() != ArgsCount) [[unlikely]] {
            throw Exception("argument count mismatch", Exception::Type::TypeError);
        }

        using UnwrapC = std::conditional_t<Trait::isConst, const C, C>;
        UnwrapC* inst = payload.unwrap<UnwrapC>();
        if (!inst) {
            throw Exception{"Accessing destroyed instance", Exception::Type::ReferenceError};
        }

        if constexpr (std::is_void_v<R>) {
            std::apply(
                [inst, &f](auto&&... unpackedArgs) {
                    (inst->*f)(std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
                },
                ConvertArgsToTuple<Tuple>(args, std::make_index_sequence<ArgsCount>())
            );
            return {}; // undefined
        } else {
            decltype(auto) ret = std::apply(
                [inst, &f](auto&&... unpackedArgs) -> R {
                    return (inst->*f)(std::forward<decltype(unpackedArgs)>(unpackedArgs)...);
                },
                ConvertArgsToTuple<Tuple>(args, std::make_index_sequence<ArgsCount>())
            );
            // 特殊情况，对于 Builder 模式，返回 this
            if constexpr (std::is_same_v<R, C&>) {
                assert(args.hasThiz() && "this is required for Builder pattern");
                return args.thiz();
            } else {
                return toJs(ret, policy, args.hasThiz() ? args.thiz() : Local<Value>{});
            }
        }
    };
}

template <size_t Len>
inline InstanceMethodCallback _mergeMethodCallbacks(std::array<InstanceMethodCallback, Len> overloads) {
    return [fs = std::move(overloads)](InstancePayload& payload, Arguments const& args) -> Local<Value> {
        return dispatchOverloadImpl<Local<Value>>(fs, payload, args);
    };
}

template <typename C, typename... Overload>
InstanceMethodCallback wrapOverloadMethodAndExtraPolicy(Overload&&... fn) {
    constexpr size_t policy_count = (static_cast<size_t>(traits::is_policy<Overload>::value) + ...);
    static_assert(policy_count <= 1, "ReturnValuePolicy can only appear once in argument list");

    ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic;
    if constexpr (policy_count > 0) {
        (
            [&](auto&& arg) {
                if constexpr (traits::is_policy<decltype(arg)>::value) {
                    policy = arg;
                }
            }(fn),
            ...
        );
    }

    constexpr size_t func_count = sizeof...(Overload) - policy_count;
    static_assert(func_count > 0, "No functions provided to overload");

    auto overloads = [&]() {
        std::array<InstanceMethodCallback, func_count> arr;
        size_t                                         idx = 0;
        (
            [&](auto&& arg) {
                if constexpr (!traits::is_policy<decltype(arg)>::value) {
                    arr[idx++] = wrapInstanceMethod<C>(std::forward<decltype(arg)>(arg), policy);
                }
            }(std::forward<Overload>(fn)),
            ...
        );
        return arr;
    }();
    return _mergeMethodCallbacks(std::move(overloads));
}


template <typename C>
InstanceMemberMeta::InstanceEqualsCallback bindInstanceEqualsImpl(std::false_type) {
    return [](void* lhs, void* rhs) -> bool { return lhs == rhs; };
}
template <typename C>
InstanceMemberMeta::InstanceEqualsCallback bindInstanceEqualsImpl(std::true_type) {
    return [](void* lhs, void* rhs) -> bool {
        if (!lhs || !rhs) return false;
        return *static_cast<C*>(lhs) == *static_cast<C*>(rhs);
    };
}
template <typename C>
InstanceMemberMeta::InstanceEqualsCallback bindInstanceEquals() {
    // use tag dispatch to fix MSVC pre name lookup or overload resolution
    return bindInstanceEqualsImpl<C>(std::bool_constant<concepts::HasEquality<C>>{});
}


} // namespace adapter


} // namespace v8kit::binding
