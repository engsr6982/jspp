#include "Reference.h"

#include "Value.h"
#include "jspp/core/ValueHelper.h"

#include <algorithm>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace jspp {


// Local<Value>
Local<Value>::Local() noexcept {
    // default constructor
    val = ValueHelper::unwrap(Undefined::newUndefined());
}

ValueKind Local<Value>::kind() const {
    if (isNull()) return ValueKind::kNull;
    if (isUndefined()) return ValueKind::kUndefined;
    if (isBoolean()) return ValueKind::kBoolean;
    if (isNumber()) return ValueKind::kNumber;
    if (isBigInt()) return ValueKind::kBigInt;
    if (isString()) return ValueKind::kString;
    if (isSymbol()) return ValueKind::kSymbol;
    if (isObject()) return ValueKind::kObject;
    if (isArray()) return ValueKind::kArray;
    if (isFunction()) return ValueKind::kFunction;
    [[unlikely]] throw std::logic_error("Unknown type, did you forget to add if branch?");
}

std::vector<std::string> Local<Object>::getOwnPropertyNamesAsString() const {
    auto keys = getOwnPropertyNames();

    std::vector<std::string> result;
    result.reserve(keys.size());
    std::transform(keys.begin(), keys.end(), std::back_inserter(result), [](Local<String> const& key) {
        return key.getValue();
    });
    return result;
}

Local<Value> Local<Array>::operator[](size_t index) const { return get(index); }

Local<Value> Local<Function>::call(Local<Value> const& thiz) const {
    return call(thiz, std::span<const Local<Value>>{});
}

Local<Value> Local<Function>::call(Local<Value> const& thiz, std::initializer_list<Local<Value>> args) const {
    return call(thiz, std::span(args));
}

Local<Value> Local<Function>::callAsConstructor() const { return callAsConstructor(std::span<const Local<Value>>{}); }

Local<Value> Local<Function>::callAsConstructor(std::initializer_list<Local<Value>> args) const {
    return callAsConstructor(std::span(args));
}


} // namespace jspp