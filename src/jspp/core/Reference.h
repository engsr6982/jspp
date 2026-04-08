#pragma once
#include "Concepts.h" // NOLINT
#include "jspp/Macro.h"

#include <cstdint>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "jspp-backend/traits/TraitReference.h"


namespace jspp {

#define SPECIALIZATION_LOCAL(VALUE)                                                                                    \
public:                                                                                                                \
    JSPP_DISABLE_NEW();                                                                                                \
    ~Local();                                                                                                          \
    Local(Local<VALUE> const&);                                                                                        \
    Local(Local<VALUE>&&) noexcept;                                                                                    \
    Local<VALUE>& operator=(Local const&);                                                                             \
    Local<VALUE>& operator=(Local&&) noexcept;                                                                         \
                                                                                                                       \
    Local<String> toString();                                                                                          \
    bool          operator==(Local<Value> const& other) const;                                                         \
                                                                                                                       \
private:                                                                                                               \
    friend struct ValueHelper;                                                                                         \
    friend class Exception;                                                                                            \
    friend class VALUE;                                                                                                \
    template <typename>                                                                                                \
    friend class Local;                                                                                                \
    template <typename>                                                                                                \
    friend class Global;                                                                                               \
    template <typename>                                                                                                \
    friend class Weak;

#define SPECALIZATION_AS_VALUE(VALUE)                                                                                  \
public:                                                                                                                \
    Local<Value> asValue() const;                                                                                      \
                 operator Local<Value>() const { return asValue(); }                                                   \
    bool         operator==(Local<VALUE> const& other) const { return operator==(other.asValue()); }

#define DECL_BACKEND_IMPL_TYPE(VALUE)                                                                                  \
private:                                                                                                               \
    using BackendType = internal::ImplType<jspp::Local<VALUE>>::type;                                                  \
    explicit Local(BackendType);                                                                                       \
    BackendType val


template <typename T>
class Local final {
    static_assert(std::is_base_of_v<Value, T>, "T must be derived from Value");
};

template <>
class Local<Value> {
    SPECIALIZATION_LOCAL(Value);
    DECL_BACKEND_IMPL_TYPE(Value);

    friend class Arguments;

public:
    Local() noexcept; // undefined

    [[nodiscard]] ValueKind kind() const;

    [[nodiscard]] bool isNull() const;
    [[nodiscard]] bool isUndefined() const;
    [[nodiscard]] bool isNullOrUndefined() const; // null or undefined
    [[nodiscard]] bool isBoolean() const;
    [[nodiscard]] bool isNumber() const;
    [[nodiscard]] bool isBigInt() const;
    [[nodiscard]] bool isString() const;
    [[nodiscard]] bool isSymbol() const;
    [[nodiscard]] bool isObject() const;
    [[nodiscard]] bool isArray() const;
    [[nodiscard]] bool isFunction() const;

    [[nodiscard]] Local<Value>     asValue() const;
    [[nodiscard]] Local<Null>      asNull() const;
    [[nodiscard]] Local<Undefined> asUndefined() const;
    [[nodiscard]] Local<Boolean>   asBoolean() const;
    [[nodiscard]] Local<Number>    asNumber() const;
    [[nodiscard]] Local<BigInt>    asBigInt() const;
    [[nodiscard]] Local<String>    asString() const;
    [[nodiscard]] Local<Symbol>    asSymbol() const;
    [[nodiscard]] Local<Object>    asObject() const;
    [[nodiscard]] Local<Array>     asArray() const;
    [[nodiscard]] Local<Function>  asFunction() const;

    /**
     * @tparam T must be the type of as described above
     */
    template <typename T>
        requires concepts::WrapType<T>
    [[nodiscard]] Local<T> as() const;

    void clear();
};

template <>
class Local<Null> {
    SPECIALIZATION_LOCAL(Null);
    SPECALIZATION_AS_VALUE(Null);
    DECL_BACKEND_IMPL_TYPE(Null);
};

template <>
class Local<Undefined> {
    SPECIALIZATION_LOCAL(Undefined);
    SPECALIZATION_AS_VALUE(Undefined);
    DECL_BACKEND_IMPL_TYPE(Undefined);
};

template <>
class Local<Boolean> {
    SPECIALIZATION_LOCAL(Boolean);
    SPECALIZATION_AS_VALUE(Boolean);
    DECL_BACKEND_IMPL_TYPE(Boolean);

public:
    [[nodiscard]] bool getValue() const;

    operator bool() const;
};

template <>
class Local<Number> {
    SPECIALIZATION_LOCAL(Number);
    SPECALIZATION_AS_VALUE(Number);
    DECL_BACKEND_IMPL_TYPE(Number);

public:
    [[nodiscard]] int    getInt32() const;
    [[nodiscard]] float  getFloat() const;
    [[nodiscard]] double getDouble() const;

    template <typename T>
        requires std::is_integral_v<T> || std::is_floating_point_v<T>
    [[nodiscard]] T getValueAs() const {
        return static_cast<T>(getDouble());
    }
};

template <>
class Local<BigInt> {
    SPECIALIZATION_LOCAL(BigInt);
    SPECALIZATION_AS_VALUE(BigInt);
    DECL_BACKEND_IMPL_TYPE(BigInt);

public:
    [[nodiscard]] int64_t  getInt64() const;
    [[nodiscard]] uint64_t getUint64() const;
};

template <>
class Local<String> {
    SPECIALIZATION_LOCAL(String);
    SPECALIZATION_AS_VALUE(String);
    DECL_BACKEND_IMPL_TYPE(String);

public:
    [[nodiscard]] int         length() const;
    [[nodiscard]] std::string getValue() const;
};

template <>
class Local<Symbol> {
    SPECIALIZATION_LOCAL(Symbol);
    SPECALIZATION_AS_VALUE(Symbol);
    DECL_BACKEND_IMPL_TYPE(Symbol);

public:
    Local<Value> getDescription(); // maybe undefined
};

template <>
class Local<Object> {
    SPECIALIZATION_LOCAL(Object);
    SPECALIZATION_AS_VALUE(Object);
    DECL_BACKEND_IMPL_TYPE(Object);

    friend class Arguments;

public:
    [[nodiscard]] bool has(Local<String> const& key) const;

    [[nodiscard]] Local<Value> get(Local<String> const& key) const;

    void set(Local<String> const& key, Local<Value> const& value);

    void remove(Local<String> const& key);

    [[nodiscard]] std::vector<Local<String>> getOwnPropertyNames() const;

    [[nodiscard]] std::vector<std::string> getOwnPropertyNamesAsString() const;

    [[nodiscard]] bool instanceof(Local<Value> const& type) const;

    [[nodiscard]] bool defineOwnProperty(
        Local<String> const& key,
        Local<Value> const&  value,
        PropertyAttribute    attrs = PropertyAttribute::None
    ) const;

    // todo: wait PropertyDescriptor implementation
    // [[nodiscard]] bool defineProperty(Local<String> const& key, PropertyDescriptor& desc) const;
};

template <>
class Local<Array> {
    SPECIALIZATION_LOCAL(Array);
    SPECALIZATION_AS_VALUE(Array);
    DECL_BACKEND_IMPL_TYPE(Array);

public:
    [[nodiscard]] size_t length() const;

    [[nodiscard]] Local<Value> get(size_t index) const;

    void set(size_t index, Local<Value> const& value);

    void push(Local<Value> const& value);

    void clear();

    Local<Value> operator[](size_t index) const;
};

template <>
class Local<Function> {
    SPECIALIZATION_LOCAL(Function);
    SPECALIZATION_AS_VALUE(Function);
    DECL_BACKEND_IMPL_TYPE(Function);

public:
    [[nodiscard]] bool isAsyncFunction() const; // JavaScript: async function

    [[nodiscard]] bool isConstructor() const; // JavaScript: class

    Local<Value> call(Local<Value> const& thiz) const;

    Local<Value> call(Local<Value> const& thiz, std::span<const Local<Value>> args) const;

    Local<Value> call(Local<Value> const& thiz, std::initializer_list<Local<Value>> args) const;

    [[nodiscard]] Local<Value> callAsConstructor() const;

    [[nodiscard]] Local<Value> callAsConstructor(std::span<const Local<Value>> args) const;

    [[nodiscard]] Local<Value> callAsConstructor(std::initializer_list<Local<Value>> args) const;
};

#undef SPECIALIZATION_LOCAL
#undef SPECALIZATION_AS_VALUE
#undef DECL_BACKEND_IMPL_TYPE


template <typename T>
class Global final {
    static_assert(std::is_base_of_v<Value, T>, "T must be derived from Value");

    using BackendImpl = internal::ImplType<Global<T>>::type;
    BackendImpl impl;

    friend Engine;
    friend Weak<T>;

public:
    JSPP_DISABLE_COPY(Global);

    Global() noexcept; // empty

    explicit Global(Local<T> const& val);
    explicit Global(Weak<T>&& val);

    Global(Global<T>&& other) noexcept;
    Global& operator=(Global<T>&& other) noexcept;

    ~Global();

    [[nodiscard]] Local<T> get() const;

    [[nodiscard]] Local<Value> getValue() const;

    [[nodiscard]] bool isEmpty() const;

    void reset();

    void reset(Local<T> const& val);

    [[nodiscard]] Engine* engine() const;
};

template <typename T>
class Weak final {
    static_assert(std::is_base_of_v<Value, T>, "T must be derived from Value");

    using BackendImpl = internal::ImplType<Weak<T>>::type;
    BackendImpl impl;

    friend Engine;
    friend Global<T>;

public:
    JSPP_DISABLE_COPY(Weak);

    Weak() noexcept; // empty

    explicit Weak(Local<T> const& val);
    explicit Weak(Global<T>&& val);

    Weak(Weak<T>&& other) noexcept;
    Weak& operator=(Weak<T>&& other) noexcept;

    ~Weak();

    [[nodiscard]] Local<T> get() const;

    [[nodiscard]] Local<Value> getValue() const;

    [[nodiscard]] bool isEmpty() const;

    void reset();

    void reset(Local<T> const& val);

    [[nodiscard]] Engine* engine() const;
};


} // namespace jspp

#include "Reference.inl" // NOLINT