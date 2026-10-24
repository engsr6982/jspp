#include "jspp/core/PromiseResolver.h"

#include "jspp/core/Value.h"


namespace jspp {


Local<Promise> Promise::resolved(Local<Value> const& value) {
    // Built on the resolver path so that thenable adoption and rejection
    // tracking follow the exact ECMA semantics on every backend.
    auto resolver = PromiseResolver::newResolver();
    auto promise  = resolver.promise();
    resolver.resolve(value);
    return promise;
}

Local<Promise> Promise::rejected(Local<Value> const& reason) {
    auto resolver = PromiseResolver::newResolver();
    auto promise  = resolver.promise();
    resolver.reject(reason);
    return promise;
}


} // namespace jspp
