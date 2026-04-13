#include "jspp/core/Engine.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Trampoline.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"


namespace jspp {

Local<Value> enable_trampoline::getOverride(Local<String> const& methodName) const {
    if (!object_ || !engine_ || object_->weak().isEmpty()) {
        return {};
    }
    auto This = object_->weak().get();

    auto overrideFunc = This.get(methodName);
    if (overrideFunc.isFunction()) {
        auto func = overrideFunc.asFunction();

        // TODO: please implement this

        // Note: It is necessary to check whether the function nativeFunctionTag_ exists.
        // If it exists, it means it is a C method, the script has not been overridden, return directly;
        // otherwise, return the method after the script has been overridden.
        throw Exception{"Not implemented yet"};
    }
    return {};
}

} // namespace jspp