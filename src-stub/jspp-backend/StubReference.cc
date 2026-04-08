#include "StubHelper.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

#include "StubReference.inl"

namespace jspp {

bool Local<Value>::isNull() const {
    // TODO: implement this
    return false;
}
bool Local<Value>::isUndefined() const {
    // TODO: implement this
    return false;
}
bool Local<Value>::isNullOrUndefined() const {
    // TODO: implement this
    return false;
}
bool Local<Value>::isBoolean() const {
    // TODO: implement this
    return false;
}
bool Local<Value>::isNumber() const {
    // TODO: implement this
    return false;
}
bool Local<Value>::isBigInt() const {
    // TODO: implement this
    return false;
}
bool Local<Value>::isString() const {
    // TODO: implement this
    return false;
}
bool Local<Value>::isSymbol() const {
    // TODO: implement this
    return false;
}
bool Local<Value>::isObject() const {
    // TODO: implement this
    return false;
}
bool Local<Value>::isArray() const {
    // TODO: implement this
    return false;
}
bool Local<Value>::isFunction() const {
    // TODO: implement this
    return false;
}


Local<Value> Local<Value>::asValue() const { return *this; }
Local<Null>  Local<Value>::asNull() const {
    if (isNull()) throw Exception{"Not Implemented"}; // TODO: implement this
    throw Exception("cannot convert to Null");
}
Local<Undefined> Local<Value>::asUndefined() const {
    if (isUndefined()) throw Exception{"Not Implemented"}; // TODO: implement this
    throw Exception("cannot convert to Undefined");
}
Local<Boolean> Local<Value>::asBoolean() const {
    if (isBoolean()) throw Exception{"Not Implemented"}; // TODO: implement this
    throw Exception("cannot convert to Boolean");
}
Local<Number> Local<Value>::asNumber() const {
    if (isNumber()) throw Exception{"Not Implemented"}; // TODO: implement this
    throw Exception("cannot convert to Number");
}
Local<BigInt> Local<Value>::asBigInt() const {
    if (isBigInt()) throw Exception{"Not Implemented"}; // TODO: implement this
    throw Exception("cannot convert to BigInt");
}
Local<String> Local<Value>::asString() const {
    if (isString()) throw Exception{"Not Implemented"}; // TODO: implement this
    throw Exception("cannot convert to String");
}
Local<Symbol> Local<Value>::asSymbol() const {
    if (isSymbol()) throw Exception{"Not Implemented"}; // TODO: implement this
    throw Exception("cannot convert to Symbol");
}
Local<Object> Local<Value>::asObject() const {
    if (isObject()) throw Exception{"Not Implemented"}; // TODO: implement this
    throw Exception("cannot convert to Object");
}
Local<Array> Local<Value>::asArray() const {
    if (isArray()) throw Exception{"Not Implemented"}; // TODO: implement this
    throw Exception("cannot convert to Array");
}
Local<Function> Local<Value>::asFunction() const {
    if (isFunction()) throw Exception{"Not Implemented"}; // TODO: implement this
    throw Exception("cannot convert to Function");
}

void Local<Value>::clear() {
    // TODO: implement this
}


#define IMPL_SPECIALIZATION_LOCAL(VALUE)                                                                               \
    Local<VALUE>::~Local() = default;                                                                                  \
    Local<VALUE>::Local(Local<VALUE> const& cp) : val(cp.val) { /* TODO: ensure copy ctor */ }                         \
    Local<VALUE>::Local(Local<VALUE>&& mv) noexcept : val(mv.val) { /* TODO: ensure move ctor */ }                     \
    Local<VALUE>& Local<VALUE>::operator=(Local const& cp) {                                                           \
        if (&cp != this) {                                                                                             \
            val = cp.val; /* TODO: ensure copy assign */                                                               \
        }                                                                                                              \
        return *this;                                                                                                  \
    }                                                                                                                  \
    Local<VALUE>& Local<VALUE>::operator=(Local&& mv) noexcept {                                                       \
        if (&mv != this) {                                                                                             \
            val = mv.val;                                                                                              \
            /* TODO: ensure move assign */                                                                             \
        }                                                                                                              \
        return *this;                                                                                                  \
    }                                                                                                                  \
    Local<String> Local<VALUE>::toString() {                                                                           \
        if (asValue().isNull()) return String::newString("null");                                                      \
        if (asValue().isUndefined()) return String::newString("undefined");                                            \
        /* TODO: implement this */                                                                                     \
        throw Exception{"Not Implemented"};                                                                            \
    }                                                                                                                  \
    bool Local<VALUE>::operator==(Local<Value> const& other) const { /* TODO: impl Strictly equal */ return false; }

#define IMPL_SPECALIZATION_AS_VALUE(VALUE)                                                                             \
    Local<Value> Local<VALUE>::asValue() const { /* TODO: impl up cast to Value */ return Local<Value>{val}; }

#define IMPL_DECL_BACKEND_IMPL_TYPE(VALUE)                                                                             \
    Local<VALUE>::Local(BackendType v8Type) : val{v8Type} {                                                            \
        /* TODO: Initialize from raw engine handle. Ensure handle validity if necessary. */                            \
    }


IMPL_SPECIALIZATION_LOCAL(Value)
IMPL_DECL_BACKEND_IMPL_TYPE(Value)

IMPL_SPECIALIZATION_LOCAL(Null);
IMPL_SPECALIZATION_AS_VALUE(Null);
IMPL_DECL_BACKEND_IMPL_TYPE(Null);

IMPL_SPECIALIZATION_LOCAL(Undefined);
IMPL_SPECALIZATION_AS_VALUE(Undefined);
IMPL_DECL_BACKEND_IMPL_TYPE(Undefined);

IMPL_SPECIALIZATION_LOCAL(Boolean);
IMPL_SPECALIZATION_AS_VALUE(Boolean);
IMPL_DECL_BACKEND_IMPL_TYPE(Boolean);
bool Local<Boolean>::getValue() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}
Local<Boolean>::operator bool() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

IMPL_SPECIALIZATION_LOCAL(Number);
IMPL_SPECALIZATION_AS_VALUE(Number);
IMPL_DECL_BACKEND_IMPL_TYPE(Number);
int Local<Number>::getInt32() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}
float Local<Number>::getFloat() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}
double Local<Number>::getDouble() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}


IMPL_SPECIALIZATION_LOCAL(BigInt);
IMPL_SPECALIZATION_AS_VALUE(BigInt);
IMPL_DECL_BACKEND_IMPL_TYPE(BigInt);
int64_t Local<BigInt>::getInt64() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}
uint64_t Local<BigInt>::getUint64() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}


IMPL_SPECIALIZATION_LOCAL(String);
IMPL_SPECALIZATION_AS_VALUE(String);
IMPL_DECL_BACKEND_IMPL_TYPE(String);
int Local<String>::length() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}
std::string Local<String>::getValue() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}


IMPL_SPECIALIZATION_LOCAL(Symbol);
IMPL_SPECALIZATION_AS_VALUE(Symbol);
IMPL_DECL_BACKEND_IMPL_TYPE(Symbol);
Local<Value> Local<Symbol>::getDescription() {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}


IMPL_SPECIALIZATION_LOCAL(Object);
IMPL_SPECALIZATION_AS_VALUE(Object);
IMPL_DECL_BACKEND_IMPL_TYPE(Object);
bool Local<Object>::has(Local<String> const& key) const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

Local<Value> Local<Object>::get(Local<String> const& key) const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

void Local<Object>::set(Local<String> const& key, Local<Value> const& value) {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

void Local<Object>::remove(Local<String> const& key) {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

std::vector<Local<String>> Local<Object>::getOwnPropertyNames() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

bool Local<Object>::instanceof(Local<Value> const& type) const {
    if (!type.isObject()) {
        return false;
    }
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

bool Local<Object>::defineOwnProperty(
    Local<String> const& key,
    Local<Value> const&  value,
    PropertyAttribute    attrs
) const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

// todo: wait PropertyDescriptor implementation
// bool Local<Object>::defineProperty(Local<String> const& key, PropertyDescriptor& desc) const {
//     // TODO: implement
//     return false;
// }


IMPL_SPECIALIZATION_LOCAL(Array);
IMPL_SPECALIZATION_AS_VALUE(Array);
IMPL_DECL_BACKEND_IMPL_TYPE(Array);
size_t Local<Array>::length() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

Local<Value> Local<Array>::get(size_t index) const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

void Local<Array>::set(size_t index, Local<Value> const& value) {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

void Local<Array>::push(Local<Value> const& value) {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

void Local<Array>::clear() {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

IMPL_SPECIALIZATION_LOCAL(Function);
IMPL_SPECALIZATION_AS_VALUE(Function);
IMPL_DECL_BACKEND_IMPL_TYPE(Function);
bool Local<Function>::isAsyncFunction() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

bool Local<Function>::isConstructor() const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

Local<Value> Local<Function>::call(Local<Value> const& thiz, std::span<const Local<Value>> args) const {
    // TODO: implement this
    throw Exception{"Not Implemented"};
}

Local<Value> Local<Function>::callAsConstructor(std::span<const Local<Value>> args) const {
    if (!isConstructor()) {
        throw std::logic_error("Local<Function>::callAsConstructor called on non-constructor");
    }
    // TODO: implement this
    throw Exception{"Not Implemented"};
}


#undef IMPL_SPECALIZATION_LOCAL
#undef IMPL_SPECALIZATION_AS_VALUE
#undef IMPL_DECL_BACKEND_IMPL_TYPE


// Display and instantiate all handles to avoid undefined symbols
//  caused by some handles not being instantiated
#define EXPLICIT_INSTANTIATE_REF(T)                                                                                    \
    template class Global<T>;                                                                                          \
    template class Weak<T>;

EXPLICIT_INSTANTIATE_REF(Value)
EXPLICIT_INSTANTIATE_REF(Null)
EXPLICIT_INSTANTIATE_REF(Undefined)
EXPLICIT_INSTANTIATE_REF(Boolean)
EXPLICIT_INSTANTIATE_REF(Number)
EXPLICIT_INSTANTIATE_REF(BigInt)
EXPLICIT_INSTANTIATE_REF(String)
EXPLICIT_INSTANTIATE_REF(Symbol)
EXPLICIT_INSTANTIATE_REF(Object)
EXPLICIT_INSTANTIATE_REF(Array)
EXPLICIT_INSTANTIATE_REF(Function)
#undef EXPLICIT_INSTANTIATE_REF

} // namespace jspp