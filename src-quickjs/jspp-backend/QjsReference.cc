#include "QjsHelper.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

#include "QjsHelper.h"
#include "QjsReference.inl"
#include "quickjs.h"


namespace jspp {

bool Local<Value>::isNull() const { return !JS_IsUninitialized(val) && JS_IsNull(val); }
bool Local<Value>::isUndefined() const { return !JS_IsUninitialized(val) && JS_IsUndefined(val); }
bool Local<Value>::isNullOrUndefined() const {
    return !JS_IsUninitialized(val) && (JS_IsNull(val) || JS_IsUndefined(val));
}
bool Local<Value>::isBoolean() const { return !JS_IsUninitialized(val) && JS_IsBool(val); }
bool Local<Value>::isNumber() const { return !JS_IsUninitialized(val) && JS_IsNumber(val); }
bool Local<Value>::isBigInt() const { return !JS_IsUninitialized(val) && JS_IsBigInt(val); }
bool Local<Value>::isString() const { return !JS_IsUninitialized(val) && JS_IsString(val); }
bool Local<Value>::isSymbol() const { return !JS_IsUninitialized(val) && JS_IsSymbol(val); }
bool Local<Value>::isObject() const { return !JS_IsUninitialized(val) && JS_IsObject(val); }
bool Local<Value>::isArray() const { return !JS_IsUninitialized(val) && JS_IsArray(val); }
bool Local<Value>::isFunction() const {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return !JS_IsUninitialized(val) && JS_IsFunction(ctx, val);
}


Local<Value> Local<Value>::asValue() const { return *this; }
Local<Null>  Local<Value>::asNull() const {
    if (isNull()) return Local<Null>{qjs_backend::QjsHelper::dupValue(val)};
    throw Exception("cannot convert to Null");
}
Local<Undefined> Local<Value>::asUndefined() const {
    if (isUndefined()) return Local<Undefined>{qjs_backend::QjsHelper::dupValue(val)};
    throw Exception("cannot convert to Undefined");
}
Local<Boolean> Local<Value>::asBoolean() const {
    if (isBoolean()) return Local<Boolean>{qjs_backend::QjsHelper::dupValue(val)};
    throw Exception("cannot convert to Boolean");
}
Local<Number> Local<Value>::asNumber() const {
    if (isNumber()) return Local<Number>{qjs_backend::QjsHelper::dupValue(val)};
    throw Exception("cannot convert to Number");
}
Local<BigInt> Local<Value>::asBigInt() const {
    if (isBigInt()) return Local<BigInt>{qjs_backend::QjsHelper::dupValue(val)};
    throw Exception("cannot convert to BigInt");
}
Local<String> Local<Value>::asString() const {
    if (isString()) return Local<String>{qjs_backend::QjsHelper::dupValue(val)};
    throw Exception("cannot convert to String");
}
Local<Symbol> Local<Value>::asSymbol() const {
    if (isSymbol()) return Local<Symbol>{qjs_backend::QjsHelper::dupValue(val)};
    throw Exception("cannot convert to Symbol");
}
Local<Object> Local<Value>::asObject() const {
    if (isObject()) return Local<Object>{qjs_backend::QjsHelper::dupValue(val)};
    throw Exception("cannot convert to Object");
}
Local<Array> Local<Value>::asArray() const {
    if (isArray()) return Local<Array>{qjs_backend::QjsHelper::dupValue(val)};
    throw Exception("cannot convert to Array");
}
Local<Function> Local<Value>::asFunction() const {
    if (isFunction()) return Local<Function>{qjs_backend::QjsHelper::dupValue(val)};
    throw Exception("cannot convert to Function");
}

void Local<Value>::clear() {
    if (!JS_IsUninitialized(val)) {
        qjs_backend::QjsHelper::freeValue(val);
        val = JS_UNDEFINED;
    }
}


#define IMPL_SPECIALIZATION_LOCAL(VALUE)                                                                               \
    Local<VALUE>::~Local() { qjs_backend::QjsHelper::freeValue(val); }                                                 \
    Local<VALUE>::Local(Local<VALUE> const& cp) : val(cp.val) { qjs_backend::QjsHelper::dupValue(val); }               \
    Local<VALUE>::Local(Local<VALUE>&& mv) noexcept : val(mv.val) { mv.val = JS_UNDEFINED; }                           \
    Local<VALUE>& Local<VALUE>::operator=(Local const& cp) {                                                           \
        if (&cp != this) {                                                                                             \
            JSValue new_val = qjs_backend::QjsHelper::dupValue(cp.val);                                                \
            qjs_backend::QjsHelper::freeValue(val);                                                                    \
            val = new_val;                                                                                             \
        }                                                                                                              \
        return *this;                                                                                                  \
    }                                                                                                                  \
    Local<VALUE>& Local<VALUE>::operator=(Local&& mv) noexcept {                                                       \
        if (&mv != this) {                                                                                             \
            qjs_backend::QjsHelper::freeValue(val);                                                                    \
            val    = mv.val;                                                                                           \
            mv.val = JS_UNDEFINED;                                                                                     \
        }                                                                                                              \
        return *this;                                                                                                  \
    }                                                                                                                  \
    Local<String> Local<VALUE>::toString() {                                                                           \
        if (asValue().isNull()) return String::newString("null");                                                      \
        if (asValue().isUndefined()) return String::newString("undefined");                                            \
        auto ctx = qjs_backend::QjsHelper::currentContextChecked();                                                    \
        auto str = JS_ToString(ctx, val);                                                                              \
        qjs_backend::QjsHelper::rethrowException(str);                                                                 \
        return Local<String>{str};                                                                                     \
    }                                                                                                                  \
    bool Local<VALUE>::operator==(Local<Value> const& other) const {                                                   \
        auto ctx = qjs_backend::QjsHelper::currentContextChecked();                                                    \
        return JS_IsStrictEqual(ctx, val, ValueHelper::unwrap(other));                                                 \
    }

#define IMPL_SPECALIZATION_AS_VALUE(VALUE)                                                                             \
    Local<Value> Local<VALUE>::asValue() const { return Local<Value>{qjs_backend::QjsHelper::dupValue(val)}; }

#define IMPL_DECL_BACKEND_IMPL_TYPE(VALUE)                                                                             \
    Local<VALUE>::Local(BackendType raw) : val{raw} {}


IMPL_SPECIALIZATION_LOCAL(Value);
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
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return JS_ToBool(ctx, val);
}
Local<Boolean>::operator bool() const { return getValue(); }

IMPL_SPECIALIZATION_LOCAL(Number);
IMPL_SPECALIZATION_AS_VALUE(Number);
IMPL_DECL_BACKEND_IMPL_TYPE(Number);
int Local<Number>::getInt32() const {
    auto ctx    = qjs_backend::QjsHelper::currentContextChecked();
    int  result = 0;
    int  code   = JS_ToInt32(ctx, &result, val);
    qjs_backend::QjsHelper::rethrowException(code);
    return result;
}
float  Local<Number>::getFloat() const { return static_cast<float>(getDouble()); }
double Local<Number>::getDouble() const {
    auto   ctx    = qjs_backend::QjsHelper::currentContextChecked();
    double result = 0;
    int    code   = JS_ToFloat64(ctx, &result, val);
    qjs_backend::QjsHelper::rethrowException(code);
    return result;
}


IMPL_SPECIALIZATION_LOCAL(BigInt);
IMPL_SPECALIZATION_AS_VALUE(BigInt);
IMPL_DECL_BACKEND_IMPL_TYPE(BigInt);
int64_t Local<BigInt>::getInt64() const {
    auto    ctx    = qjs_backend::QjsHelper::currentContextChecked();
    int64_t result = 0;
    int     code   = JS_ToBigInt64(ctx, &result, val);
    qjs_backend::QjsHelper::rethrowException(code);
    return result;
}
uint64_t Local<BigInt>::getUint64() const {
    auto     ctx    = qjs_backend::QjsHelper::currentContextChecked();
    uint64_t result = 0;
    int      code   = JS_ToBigUint64(ctx, &result, val);
    qjs_backend::QjsHelper::rethrowException(code);
    return result;
}


IMPL_SPECIALIZATION_LOCAL(String);
IMPL_SPECALIZATION_AS_VALUE(String);
IMPL_DECL_BACKEND_IMPL_TYPE(String);
int Local<String>::length() const {
    auto    ctx = qjs_backend::QjsHelper::currentContextChecked();
    int64_t len;
    int     code = JS_GetLength(ctx, val, &len);
    qjs_backend::QjsHelper::rethrowException(code);
    return static_cast<int>(len);
}
std::string Local<String>::getValue() const {
    auto   ctx = qjs_backend::QjsHelper::currentContextChecked();
    size_t len{};
    auto   cstr = JS_ToCStringLen(ctx, &len, val);
    if (cstr == nullptr) [[unlikely]] {
        throw Exception{"Failed to convert String to std::string", ExceptionType::Error};
    }
    std::string copy{cstr, len};
    JS_FreeCString(ctx, cstr);
    return copy;
}


IMPL_SPECIALIZATION_LOCAL(Symbol);
IMPL_SPECALIZATION_AS_VALUE(Symbol);
IMPL_DECL_BACKEND_IMPL_TYPE(Symbol);
Local<Value> Local<Symbol>::getDescription() {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    // TODO: Optimize the implementation of this function
    // tigger Symbol.prototype.description getter
    auto desc = JS_GetPropertyStr(ctx, val, "description");
    // Symbol() -> JS_UNDEFINED
    // Symbol('foo') -> JS_TAG_STRING
    return Local<Value>{desc};
}


IMPL_SPECIALIZATION_LOCAL(Object);
IMPL_SPECALIZATION_AS_VALUE(Object);
IMPL_DECL_BACKEND_IMPL_TYPE(Object);
bool Local<Object>::has(Local<String> const& key) const {
    auto ctx  = qjs_backend::QjsHelper::currentContextChecked();
    auto atom = JS_ValueToAtom(ctx, ValueHelper::unwrap(key));

    auto ret = JS_HasProperty(ctx, val, atom);
    JS_FreeAtom(ctx, atom);

    qjs_backend::QjsHelper::rethrowException(ret);
    return ret != 0;
}

Local<Value> Local<Object>::get(Local<String> const& key) const {
    auto ctx  = qjs_backend::QjsHelper::currentContextChecked();
    auto atom = JS_ValueToAtom(ctx, ValueHelper::unwrap(key));

    auto ret = JS_GetProperty(ctx, val, atom);
    JS_FreeAtom(ctx, atom);

    qjs_backend::QjsHelper::rethrowException(ret);
    return Local<Value>{ret};
}

void Local<Object>::set(Local<String> const& key, Local<Value> const& value) {
    auto ctx  = qjs_backend::QjsHelper::currentContextChecked();
    auto atom = JS_ValueToAtom(ctx, ValueHelper::unwrap(key));
    auto ret  = JS_SetProperty(ctx, val, atom, qjs_backend::QjsHelper::getDupLocal(value, ctx));
    JS_FreeAtom(ctx, atom);

    qjs_backend::QjsHelper::rethrowException(ret);
}

void Local<Object>::remove(Local<String> const& key) {
    auto ctx  = qjs_backend::QjsHelper::currentContextChecked();
    auto atom = JS_ValueToAtom(ctx, ValueHelper::unwrap(key));
    auto ret  = JS_DeleteProperty(ctx, val, atom, 0);
    JS_FreeAtom(ctx, atom);
    qjs_backend::QjsHelper::rethrowException(ret);
}

std::vector<Local<String>> Local<Object>::getOwnPropertyNames() const {
    auto            ctx  = qjs_backend::QjsHelper::currentContextChecked();
    JSPropertyEnum* ptab = nullptr;
    uint32_t        len  = 0;

    qjs_backend::QjsHelper::rethrowException(
        JS_GetOwnPropertyNames(ctx, &ptab, &len, val, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK | JS_GPN_PRIVATE_MASK)
    );

    std::unique_ptr<JSPropertyEnum, std::function<void(JSPropertyEnum*)>> ptr(ptab, [ctx](JSPropertyEnum* list) {
        if (list) js_free(ctx, list);
    });

    std::vector<Local<String>> ret;
    ret.reserve(len);
    for (uint32_t i = 0; i < len; ++i) {
        ret.push_back(Local<String>{JS_AtomToString(ctx, ptab[i].atom)});
        JS_FreeAtom(ctx, ptab[i].atom);
    }
    return ret;
}

bool Local<Object>::instanceof(Local<Value> const& type) const {
    if (!type.isObject()) {
        return false;
    }
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto ret = JS_IsInstanceOf(ctx, val, ValueHelper::unwrap(type));
    qjs_backend::QjsHelper::rethrowException(ret);
    return ret != 0;
}

bool Local<Object>::defineOwnProperty(
    Local<String> const& key,
    Local<Value> const&  value,
    PropertyAttribute    attrs
) const {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();

    auto atom = JS_ValueToAtom(ctx, ValueHelper::unwrap(key));

    int ret = JS_DefinePropertyValue(
        ctx,
        val,
        atom,
        qjs_backend::QjsHelper::getDupLocal(value, ctx),
        qjs_backend::QjsHelper::castAttribute(attrs)
    );
    JS_FreeAtom(ctx, atom);
    qjs_backend::QjsHelper::rethrowException(ret);
    return ret != 0;
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
    auto    ctx = qjs_backend::QjsHelper::currentContextChecked();
    int64_t len;
    int     code = JS_GetLength(ctx, val, &len);
    qjs_backend::QjsHelper::rethrowException(code);
    return static_cast<int>(len);
}

Local<Value> Local<Array>::get(size_t index) const {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto ret = JS_GetPropertyUint32(ctx, val, static_cast<uint32_t>(index));
    qjs_backend::QjsHelper::rethrowException(ret);
    return Local<Value>{ret};
}

void Local<Array>::set(size_t index, Local<Value> const& value) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    int  code =
        JS_SetPropertyInt64(ctx, val, static_cast<int64_t>(index), qjs_backend::QjsHelper::getDupLocal(value, ctx));
    qjs_backend::QjsHelper::rethrowException(code);
}

void Local<Array>::push(Local<Value> const& value) { set(length(), value); }

void Local<Array>::clear() {
    auto ctx  = qjs_backend::QjsHelper::currentContextChecked();
    int  code = JS_SetLength(ctx, val, 0);
    qjs_backend::QjsHelper::rethrowException(code);
}

IMPL_SPECIALIZATION_LOCAL(Function);
IMPL_SPECALIZATION_AS_VALUE(Function);
IMPL_DECL_BACKEND_IMPL_TYPE(Function);
bool Local<Function>::isAsyncFunction() const {
    // In QuickJs, there is no native interface to determine asynchronous functions.
    // Asynchronous functions have their own ClassID and Atom (AsyncFunction).
    // However, QuickJs does not expose this ClassID.

#if QJS_VERSION_MAJOR > 0 || (QJS_VERSION_MAJOR == 0 && QJS_VERSION_MINOR >= 14)
    // O(1) fast path
    return JS_IsAsyncFunction(val);
#else
    // fallback
    // TODO: PR #1446 has been merged into QuickJs-NG v0.14.0, removing this fallback code
    // https://github.com/quickjs-ng/quickjs/pull/1446
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto rt  = JS_GetRuntime(ctx);

    JSClassID classId = JS_GetClassID(this->val);
    if (classId == 0) {
        return false; // not a object
    }

    JSAtom classNameAtom = JS_GetClassName(rt, classId);
    if (classNameAtom == JS_ATOM_NULL) {
        return false;
    }

    JSAtom targetAtom = JS_NewAtom(ctx, "AsyncFunction");

    bool isAsync = (classNameAtom == targetAtom);

    JS_FreeAtom(ctx, targetAtom);
    JS_FreeAtom(ctx, classNameAtom);
    return isAsync;
#endif
}

bool Local<Function>::isConstructor() const {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return JS_IsConstructor(ctx, val);
}

Local<Value> Local<Function>::call(Local<Value> const& thiz, std::span<const Local<Value>> args) const {
    static_assert(sizeof(Local<Value>) == sizeof(JSValue), "Local<Value> and JSValue must have the same size");

    int  argc = static_cast<int>(args.size());
    auto argv = reinterpret_cast<JSValueConst*>(const_cast<Local<Value>*>(args.data()));

    auto ctx = qjs_backend::QjsHelper::currentContextChecked();

    auto ret = JS_Call(ctx, val, qjs_backend::QjsHelper::peekValue(thiz), argc, argv);
    qjs_backend::QjsHelper::rethrowException(ret);
    EngineScope::currentEngineChecked().pumpPendingJobs();
    return Local<Value>{ret};
}

Local<Value> Local<Function>::callAsConstructor(std::span<const Local<Value>> args) const {
    if (!isConstructor()) {
        throw std::logic_error("Local<Function>::callAsConstructor called on non-constructor");
    }
    static_assert(sizeof(Local<Value>) == sizeof(JSValue), "Local<Value> and JSValue must have the same size");

    int  argc = static_cast<int>(args.size());
    auto argv = reinterpret_cast<JSValueConst*>(const_cast<Local<Value>*>(args.data()));

    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto ret = JS_CallConstructor(ctx, val, argc, argv);
    qjs_backend::QjsHelper::rethrowException(ret);
    EngineScope::currentEngineChecked().pumpPendingJobs();
    return Local<Value>{ret};
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