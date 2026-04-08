#include "StubHelper.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
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
Local<Number> Number::newNumber(int i) {
    // TODO: please implement this
    throw Exception{"Not implemented"};
}
Local<Number> Number::newNumber(float f) {
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