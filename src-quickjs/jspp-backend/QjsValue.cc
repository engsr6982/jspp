#include "QjsEngine.h"
#include "QjsHelper.h"
#include "jspp-backend/QjsHelper.h"
#include "jspp/Macro.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Fwd.h"
#include "jspp/core/PromiseResolver.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

#include <array>
#include <cassert>
#include <cstdint>
#include <memory>


JSPP_WARNING_GUARD_BEGIN
#include "quickjs.h"
JSPP_WARNING_GUARD_END

#include <string_view>
#include <utility>


namespace jspp {


Local<Null> Null::newNull() { return Local<Null>{JS_NULL}; }


Local<Undefined> Undefined::newUndefined() { return Local<Undefined>{JS_UNDEFINED}; }


Local<Boolean> Boolean::newBoolean(bool b) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return Local<Boolean>{JS_NewBool(ctx, b)};
}


Local<Number> Number::newNumber(double d) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return Local<Number>{JS_NewFloat64(ctx, d)};
}
Local<Number> Number::newNumber(int32_t i32) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return Local<Number>{JS_NewInt32(ctx, i32)};
}
Local<Number> Number::newNumber(float f) { return newNumber(static_cast<double>(f)); }
Local<Number> Number::newNumber(uint32_t u32) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return Local<Number>{JS_NewUint32(ctx, u32)};
}

Local<BigInt> BigInt::newBigInt(int64_t i) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return Local<BigInt>{JS_NewBigInt64(ctx, i)};
}
Local<BigInt> BigInt::newBigIntUnsigned(uint64_t i) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    return Local<BigInt>{JS_NewBigUint64(ctx, i)};
}


MaybeLocal<String> String::newString(const char* str) { return newString(std::string_view{str}); }
MaybeLocal<String> String::newString(std::string const& str) { return newString(std::string_view{str}); }
MaybeLocal<String> String::newString(std::string_view sv) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();

    auto str = JS_NewStringLen(ctx, sv.data(), sv.size());
    return qjs_backend::QjsHelper::asMaybeLocal<String>(str);
}

MaybeLocal<Symbol> Symbol::newSymbol() {
    // The Symbol API of QuickJs-NG does not support Symbols without descriptions
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
#if QJS_VERSION_MAJOR > 0 || (QJS_VERSION_MAJOR == 0 && QJS_VERSION_MINOR >= 15)
    // v0.15.0+ fast path (PR #1447) — safe to pass NULL
    auto sym = JS_NewSymbol(ctx, nullptr, false);
    return qjs_backend::QjsHelper::asMaybeLocal<Symbol>(sym);
#else
    // fallback
    // TODO: PR #1447 has been merged into the QuickJs-NG mainline.
    // This fallback code will be removed after the release of v0.15.0.
    // https://github.com/quickjs-ng/quickjs/pull/1447
    auto global  = JS_GetGlobalObject(ctx);
    auto symCtor = JS_GetPropertyStr(ctx, global, "Symbol");
    JS_FreeValue(ctx, global);
    if (qjs_backend::QjsHelper::isException(symCtor)) {
        return qjs_backend::QjsHelper::asMaybeLocal<Symbol>(symCtor);
    }

    auto sym = JS_Call(ctx, symCtor, JS_UNDEFINED, 0, nullptr);
    JS_FreeValue(ctx, symCtor);

    auto ret = qjs_backend::QjsHelper::asMaybeLocal<Symbol>(sym);
    if (ret) {
        EngineScope::currentEngineChecked().pumpPendingJobs();
    }
    return ret;
#endif
}
MaybeLocal<Symbol> Symbol::newSymbol(std::string_view description) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    // is_global = true => Symbol.for()
    auto sym = JS_NewSymbol(ctx, description.data(), false);
    return qjs_backend::QjsHelper::asMaybeLocal<Symbol>(sym);
}
MaybeLocal<Symbol> Symbol::newSymbol(const char* description) { return newSymbol(std::string_view{description}); }
MaybeLocal<Symbol> Symbol::newSymbol(std::string const& description) {
    return newSymbol(std::string_view{description});
}

MaybeLocal<Symbol> Symbol::forKey(Local<String> const& str) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    // Since quickjs-ng does not export the JS_NewSymbolFromAtom interface,
    //  we can only fall back to the upper-level interface here.
    // This results in meaningless conversion overhead.
    // TODO: To be optimized.
    auto stdStr = str.getValue();
    auto sym    = JS_NewSymbol(ctx, stdStr.data(), true);
    return qjs_backend::QjsHelper::asMaybeLocal<Symbol>(sym);
}


MaybeLocal<Function> Function::newFunction(FunctionCallback&& cb) {
    auto ptr = std::make_unique<FunctionCallback>(std::move(cb));

    auto& engine = EngineScope::currentEngineChecked();

    auto data = JS_NewObjectClass(engine.context_, engine.functionDataClassId_);
    if (qjs_backend::QjsHelper::isException(data)) {
        return qjs_backend::QjsHelper::asMaybeLocal<Function>(data);
    }
    JS_SetOpaque(data, ptr.release());

    auto engineOpaque = engine.newPointerData(&engine);

    auto args = std::array{engineOpaque, data};

    auto func = JS_NewCFunctionData(
        engine.context_,
        [](JSContext* ctx, JSValueConst thiz, int argc, JSValueConst* argv, int /* magic */, JSValue* data) -> JSValue {
            auto externalPointerID = JS_GetClassID(data[0]);
            assert(externalPointerID != JS_INVALID_CLASS_ID);

            auto engine = static_cast<Engine*>(JS_GetOpaque(data[0], externalPointerID));
            if (externalPointerID != engine->pointerDataClassId_) [[unlikely]] {
                JS_ThrowTypeError(ctx, "Invalid engine pointer data");
                return JS_EXCEPTION;
            }

            auto callback = static_cast<FunctionCallback*>(JS_GetOpaque(data[1], engine->functionDataClassId_));

            EngineScope tracker{engine}; // for addon
            try {
                auto args   = qjs_backend::QjsHelper::wrapArguments(engine, thiz, argc, argv);
                auto result = (*callback)(args);
                return qjs_backend::QjsHelper::getDupLocal(result);
            } catch (Exception const& e) {
                return qjs_backend::QjsHelper::rethrowToScript(e);
            } catch (std::exception const& e) {
                return qjs_backend::QjsHelper::rethrowToScript(e, engine);
            } catch (...) {
                JS_ThrowPlainError(engine->context_, "Unknown C++ exception occurred");
                return JS_EXCEPTION;
            }
        },
        0,
        0,
        args.size(),
        args.data()
    );
    for (auto& arg : args) JS_FreeValue(engine.context_, arg);
    return qjs_backend::QjsHelper::asMaybeLocal<Function>(func);
}


MaybeLocal<Object> Object::newObject() {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto obj = JS_NewObject(ctx);
    return qjs_backend::QjsHelper::asMaybeLocal<Object>(obj);
}


MaybeLocal<Array> Array::newArray(size_t length) {
    auto ctx   = qjs_backend::QjsHelper::currentContextChecked();
    auto array = JS_NewArray(ctx);
    if (qjs_backend::QjsHelper::isException(array)) {
        return qjs_backend::QjsHelper::asMaybeLocal<Array>(array);
    }
    if (length > 0) {
        auto code = JS_SetLength(ctx, array, static_cast<int64_t>(length));
        if (code < 0) [[unlikely]] {
            JS_FreeValue(ctx, array);
            return qjs_backend::QjsHelper::asMaybeLocal<Array>(code);
        }
    }
    return ValueHelper::wrapMaybeLocal<Array>(array);
}


PromiseResolver PromiseResolver::newResolver() {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();

    JSValue funcs[2]{};
    auto    promise = JS_NewPromiseCapability(ctx, funcs);
    qjs_backend::QjsHelper::rethrowException(promise);

    PromiseResolver resolver;
    {
        auto local = ValueHelper::wrap<Promise>(promise); // adopts the new reference
        resolver.impl_.promise_.reset(local);             // Global::reset dups internally
    }
    {
        auto local = ValueHelper::wrap<Function>(funcs[0]);
        resolver.impl_.resolve_.reset(local);
    }
    {
        auto local = ValueHelper::wrap<Function>(funcs[1]);
        resolver.impl_.reject_.reset(local);
    }
    return resolver;
}

bool PromiseResolver::isEmpty() const noexcept { return impl_.promise_.isEmpty(); }

void PromiseResolver::reset() {
    impl_.resolve_.reset();
    impl_.reject_.reset();
    impl_.promise_.reset();
}

Local<Promise> PromiseResolver::promise() const {
    if (isEmpty()) {
        throw Exception{"PromiseResolver::promise(): resolver is empty"};
    }
    return impl_.promise_.get();
}

void PromiseResolver::resolve(Local<Value> const& value) const {
    if (isEmpty()) {
        throw Exception{"PromiseResolver::resolve(): resolver is empty"};
    }
    // Calling the capability function gives exact ECMA semantics for free:
    // alreadyResolved idempotence, thenable adoption and rejection tracking.
    (void)impl_.resolve_.get().call(Local<Value>{}, {value});
}

void PromiseResolver::reject(Local<Value> const& reason) const {
    if (isEmpty()) {
        throw Exception{"PromiseResolver::reject(): resolver is empty"};
    }
    (void)impl_.reject_.get().call(Local<Value>{}, {reason});
}

MaybeLocal<Proxy> Proxy::newProxy(Local<Object> const& target, Local<Object> const& handler) {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto proxy =
        JS_NewProxy(ctx, qjs_backend::QjsHelper::peekValue(target), qjs_backend::QjsHelper::peekValue(handler));
    return qjs_backend::QjsHelper::asMaybeLocal<Proxy>(proxy);
}

Local<Value> Local<Proxy>::target() const {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto ret = JS_GetProxyTarget(ctx, val); // returns a new reference
    if (JS_IsException(ret)) {
        // A revoked proxy throws here; report null per the API contract.
        JS_FreeValue(ctx, JS_GetException(ctx));
        return Local<Value>{JS_NULL};
    }
    return Local<Value>{ret};
}

Local<Value> Local<Proxy>::handler() const {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto ret = JS_GetProxyHandler(ctx, val); // returns a new reference
    if (JS_IsException(ret)) {
        JS_FreeValue(ctx, JS_GetException(ctx));
        return Local<Value>{JS_NULL};
    }
    return Local<Value>{ret};
}

PromiseState Local<Promise>::state() const {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    switch (JS_PromiseState(ctx, val)) {
    case JS_PROMISE_PENDING:
        return PromiseState::kPending;
    case JS_PROMISE_FULFILLED:
        return PromiseState::kFulfilled;
    case JS_PROMISE_REJECTED:
        return PromiseState::kRejected;
    default:
        [[unlikely]] throw Exception{"Local<Promise>::state(): value is not a promise"};
    }
}

MaybeLocal<Value> Local<Promise>::result() const {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    if (JS_PromiseState(ctx, val) == JS_PROMISE_PENDING) {
        return MaybeLocal<Value>::fromError("Local<Promise>::result(): promise is pending");
    }
    return Local<Value>{JS_PromiseResult(ctx, val)}; // returns a new reference
}

MaybeLocal<Promise> Local<Promise>::then(Local<Function> const& onFulfilled, Local<Function> const& onRejected) const {
#if QJS_VERSION_MAJOR > 0 || (QJS_VERSION_MAJOR == 0 && QJS_VERSION_MINOR >= 16)
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto ret = JS_PromiseThen(
        ctx,
        val,
        qjs_backend::QjsHelper::peekValue(onFulfilled),
        qjs_backend::QjsHelper::peekValue(onRejected)
    );
    auto maybe = qjs_backend::QjsHelper::asMaybeLocal<Promise>(ret);
    if (maybe) {
        EngineScope::currentEngineChecked().pumpPendingJobs();
    }
    return maybe;
#else
    // Compatibility fallback for QuickJS-NG < 0.16.0.
    // WARNING: This path is NOT semantically equivalent to JS_PromiseThen.
    // Calling Promise.prototype.then may be affected by user overrides
    // and Symbol.species, potentially diverging from PerformPromiseThen.
    // Prefer QuickJS-NG >= 0.16.0 for native semantics.
    // See: https://github.com/quickjs-ng/quickjs/pull/1605
    //
    // TODO: Remove this fallback once the minimum supported QuickJS-NG
    // version is raised to >= 0.16.0.
    auto maybeObj = asValue().asObject();
    if (!maybeObj) {
        return MaybeLocal<Promise>::forward(maybeObj);
    }
    auto thenFnName = String::newString("then");
    if (!thenFnName) {
        return MaybeLocal<Promise>::fromError("Local<Promise>::then(): failed to allocate 'then' string");
    }
    auto thenFn = maybeObj.toLocal().get(thenFnName.toLocal()).asFunction();
    if (!thenFn) {
        return MaybeLocal<Promise>::fromError("Local<Promise>::then(): 'then' is not a function");
    }
    auto result = thenFn.toLocal().call(asValue(), {onFulfilled.asValue(), onRejected.asValue()});
    return result.as<Promise>();
#endif
}

MaybeLocal<Promise> Local<Promise>::then(Local<Function> const& onFulfilled) const {
#if QJS_VERSION_MAJOR > 0 || (QJS_VERSION_MAJOR == 0 && QJS_VERSION_MINOR >= 16)
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto ret = JS_PromiseThen(ctx, val, qjs_backend::QjsHelper::peekValue(onFulfilled), JS_UNDEFINED);
    qjs_backend::QjsHelper::rethrowException(ret);
    EngineScope::currentEngineChecked().pumpPendingJobs();
    return ValueHelper::wrap<Promise>(ret);
#else
    // Compatibility fallback for QuickJS-NG < 0.16.0 - see the full warning
    // on then(onFulfilled, onRejected) above.
    // TODO: Remove this fallback once the minimum supported QuickJS-NG
    // version is raised to >= 0.16.0.
    auto thenFn = asValue().asObject().get(String::newString("then")).asFunction();
    auto result = thenFn.call(asValue(), {onFulfilled.asValue(), Undefined::newUndefined()});
    return result.as<Promise>();
#endif
}

Local<Promise> Local<Promise>::catchError(Local<Function> const& onRejected) const {
#if QJS_VERSION_MAJOR > 0 || (QJS_VERSION_MAJOR == 0 && QJS_VERSION_MINOR >= 16)
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    auto ret = JS_PromiseThen(ctx, val, JS_UNDEFINED, qjs_backend::QjsHelper::peekValue(onRejected));
    qjs_backend::QjsHelper::rethrowException(ret);
    EngineScope::currentEngineChecked().pumpPendingJobs();
    return ValueHelper::wrap<Promise>(ret);
#else
    // Compatibility fallback for QuickJS-NG < 0.16.0 - see the full warning
    // on then(onFulfilled, onRejected) above.
    // TODO: Remove this fallback once the minimum supported QuickJS-NG
    // version is raised to >= 0.16.0.
    auto catchFn = asValue().asObject().get(String::newString("catch")).asFunction();
    auto result  = catchFn.call(asValue(), {onRejected.asValue()});
    return result.as<Promise>();
#endif
}


Arguments::Arguments(BackendImpl impl) : impl_(std::move(impl)) {}

Engine* Arguments::runtime() const { return impl_.engine_; }

bool Arguments::hasThiz() const { return JS_IsObject(impl_.thiz_); }

Local<Object> Arguments::thiz() const {
    if (!hasThiz()) {
        throw Exception{"Arguments::thiz(): no thiz"};
    }
    return Local<Object>{qjs_backend::QjsHelper::dupValue(impl_.thiz_, impl_.engine_->context_)};
}

size_t Arguments::length() const { return static_cast<size_t>(impl_.argc_); }

Local<Value> Arguments::operator[](size_t index) const {
    if (index >= length()) {
        return {}; // undefined
    }
    return Local<Value>{qjs_backend::QjsHelper::dupValue(impl_.argv_[index], impl_.engine_->context_)};
}


} // namespace jspp