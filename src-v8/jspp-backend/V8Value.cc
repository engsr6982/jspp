#include "V8Helper.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"


#include <string_view>
#include <utility>


JSPP_WARNING_GUARD_BEGIN
#include <v8-exception.h>
#include <v8-external.h>
#include <v8-function-callback.h>
#include <v8-local-handle.h>
#include <v8-primitive.h>
#include <v8-template.h>
#include <v8-value.h>
JSPP_WARNING_GUARD_END

namespace jspp {


Local<Null> Null::newNull() {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<Null>{v8::Null(isolate)};
}


Local<Undefined> Undefined::newUndefined() {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<Undefined>{v8::Undefined(isolate)};
}


Local<Boolean> Boolean::newBoolean(bool b) {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<Boolean>{v8::Boolean::New(isolate, b)};
}


Local<Number> Number::newNumber(double d) {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<Number>{v8::Number::New(isolate, d)};
}
Local<Number> Number::newNumber(int i) {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<Number>{v8::Number::New(isolate, i)};
}
Local<Number> Number::newNumber(float f) {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<Number>{v8::Number::New(isolate, f)};
}


Local<BigInt> BigInt::newBigInt(int64_t i) {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<BigInt>{v8::BigInt::New(isolate, i)};
}
Local<BigInt> BigInt::newBigIntUnsigned(uint64_t i) {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<BigInt>{v8::BigInt::NewFromUnsigned(isolate, i)};
}


Local<String> String::newString(const char* str) { return newString(std::string_view{str}); }
Local<String> String::newString(std::string const& str) { return newString(std::string_view{str}); }
Local<String> String::newString(std::string_view str) {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();

    v8::TryCatch vtry{isolate};

    auto v8Str =
        v8::String::NewFromUtf8(isolate, str.data(), v8::NewStringType::kNormal, static_cast<int>(str.length()));
    v8_backend::V8Helper::rethrowException(vtry);
    return Local<String>{v8Str.ToLocalChecked()};
}

Local<Symbol> Symbol::newSymbol() {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<Symbol>{v8::Symbol::New(isolate)};
}
Local<Symbol> Symbol::newSymbol(std::string_view description) {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    auto v8Sym   = v8::Symbol::New(isolate, ValueHelper::unwrap(String::newString(description)));
    return Local<Symbol>{v8Sym};
}
Local<Symbol> Symbol::newSymbol(const char* description) { return newSymbol(std::string_view{description}); }
Local<Symbol> Symbol::newSymbol(std::string const& description) { return newSymbol(description.c_str()); }

Local<Symbol> Symbol::forKey(Local<String> const& str) {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<Symbol>{v8::Symbol::For(isolate, ValueHelper::unwrap(str))};
}


Local<Function> Function::newFunction(FunctionCallback&& cb) {
    struct AssociateResources {
        Engine*          runtime{nullptr};
        FunctionCallback cb;
    };

    auto&& [isolate, ctx] = v8_backend::V8Helper::currentIsolateContextChecked();

    auto vtry = v8::TryCatch{isolate};
    auto data = std::make_unique<AssociateResources>(EngineScope::currentEngine(), std::move(cb));

    auto external = v8::External::New(isolate, static_cast<void*>(data.get())).As<v8::Value>();
    auto temp     = v8::FunctionTemplate::New(
        isolate,
        [](v8::FunctionCallbackInfo<v8::Value> const& info) {
            auto data = reinterpret_cast<AssociateResources*>(info.Data().As<v8::External>()->Value());
            auto args = Arguments{std::make_pair(data->runtime, info)};
            try {
                auto returnValue = data->cb(args); // call native
                info.GetReturnValue().Set(ValueHelper::unwrap(returnValue));
            } catch (Exception const& e) {
                v8_backend::V8Helper::rethrowToScript(e);
            }
        },
        external
    );
    temp->RemovePrototype();

    auto v8Func = temp->GetFunction(ctx);
    v8_backend::V8Helper::rethrowException(vtry);

    EngineScope::currentEngineChecked().addManagedResource(data.release(), v8Func.ToLocalChecked(), [](void* data) {
        delete reinterpret_cast<AssociateResources*>(data);
    });

    return Local<Function>{v8Func.ToLocalChecked()};
}


Local<Object> Object::newObject() {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<Object>{v8::Object::New(isolate)};
}


Local<Array> Array::newArray(size_t length) {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();
    return Local<Array>{v8::Array::New(isolate, static_cast<int>(length))};
}


Arguments::Arguments(BackendImpl impl) : impl_(std::move(impl)) {}

Engine* Arguments::runtime() const { return impl_.first; }

bool Arguments::hasThiz() const { return impl_.second.This()->IsObject(); }

Local<Object> Arguments::thiz() const {
    if (!hasThiz()) {
        throw Exception{"Arguments::thiz(): no thiz"};
    }
    return Local<Object>{impl_.second.This()};
}

size_t Arguments::length() const { return static_cast<size_t>(impl_.second.Length()); }

Local<Value> Arguments::operator[](size_t index) const {
    auto value = impl_.second[static_cast<int>(index)];
    return Local<Value>{value};
}


} // namespace jspp