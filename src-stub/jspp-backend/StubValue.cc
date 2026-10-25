#include "StubHelper.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/PromiseResolver.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"


#include <string_view>
#include <utility>


namespace jspp {


Local<Null> Null::newNull() {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}


Local<Undefined> Undefined::newUndefined() {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}


Local<Boolean> Boolean::newBoolean(bool b) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}


Local<Number> Number::newNumber(double d) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}
Local<Number> Number::newNumber(int32_t i32) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}
Local<Number> Number::newNumber(float f) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}
Local<Number> Number::newNumber(uint32_t u32) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

Local<BigInt> BigInt::newBigInt(int64_t i) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}
Local<BigInt> BigInt::newBigIntUnsigned(uint64_t i) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}


Local<String> String::newString(const char* str) { return newString(std::string_view{str}); }
Local<String> String::newString(std::string const& str) { return newString(std::string_view{str}); }
Local<String> String::newString(std::string_view str) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

Local<Symbol> Symbol::newSymbol() {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}
Local<Symbol> Symbol::newSymbol(std::string_view description) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}
Local<Symbol> Symbol::newSymbol(const char* description) { return newSymbol(std::string_view{description}); }
Local<Symbol> Symbol::newSymbol(std::string const& description) { return newSymbol(description.c_str()); }

Local<Symbol> Symbol::forKey(Local<String> const& str) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}


Local<Function> Function::newFunction(FunctionCallback&& cb) {
    // TODO: please implement this
    // Before invoking the user's method, you need to try to catch possible Exception exceptions
    // Bypassing is fine, please remove the prototype chain of this function
    throw Exception{"Not implemented"};
}


Local<Object> Object::newObject() {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}


Local<Array> Array::newArray(size_t length) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}


PromiseResolver PromiseResolver::newResolver() {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

bool PromiseResolver::isEmpty() const noexcept {
    // TODO: please implement this
    return true;
}

void PromiseResolver::reset() {
    // TODO: please implement this
}

Local<Promise> PromiseResolver::promise() const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

void PromiseResolver::resolve(Local<Value> const& value) const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

void PromiseResolver::reject(Local<Value> const& reason) const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

Local<Proxy> Proxy::newProxy(Local<Object> const& target, Local<Object> const& handler) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

Local<Value> Local<Proxy>::target() const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

Local<Value> Local<Proxy>::handler() const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

PromiseState Local<Promise>::state() const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

Local<Value> Local<Promise>::result() const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

Local<Promise> Local<Promise>::then(Local<Function> const& onFulfilled, Local<Function> const& onRejected) const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

Local<Promise> Local<Promise>::then(Local<Function> const& onFulfilled) const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

Local<Promise> Local<Promise>::catchError(Local<Function> const& onRejected) const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}


Arguments::Arguments(BackendImpl impl) : impl_(std::move(impl)) {}

Engine* Arguments::runtime() const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

bool Arguments::hasThiz() const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

Local<Object> Arguments::thiz() const {
    if (!hasThiz()) {
        throw Exception{"Arguments::thiz(): no thiz"};
    }
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

size_t Arguments::length() const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}

Local<Value> Arguments::operator[](size_t index) const {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}


} // namespace jspp