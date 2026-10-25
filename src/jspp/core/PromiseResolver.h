#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Reference.h"

#include "jspp-backend/traits/TraitPromise.h"

namespace jspp {

/**
 * @brief C++ handle of an ECMA-262 PromiseCapability Record: holds a pending
 *        promise together with its resolve/reject capability.
 *
 * The promise is a native engine promise, so await, then-chaining and
 * microtask ordering follow the engine (ECMA conformant).
 *
 * @note  Movable, not copyable. May be moved across threads; resolve()/reject()
 *        must run inside an EngineScope of the owning engine. QuickJS runtimes
 *        are single threaded: EngineScope serializes access with the engine
 *        mutex and refreshes the stack top on every thread switch
 *        (JS_UpdateStackTop), so access is sequential, never concurrent.
 *        V8 serializes with v8::Locker.
 * @note  Lifetime follows Global<T>: a resolver must not outlive its Engine.
 * @note  Settling twice is a silent no-op (ECMA alreadyResolved flag).
 */
class PromiseResolver final {
public:
    JSPP_DISABLE_COPY(PromiseResolver);

    PromiseResolver() noexcept = default;
    PromiseResolver(PromiseResolver&&) noexcept = default;
    PromiseResolver& operator=(PromiseResolver&&) noexcept = default;
    ~PromiseResolver() = default;

    /** @brief Create a pending promise and its settle capability (requires EngineScope). */
    [[nodiscard]] static PromiseResolver newResolver();

    [[nodiscard]] bool isEmpty() const noexcept;

    /** @brief Release the capability; the promise stays pending and is left to the GC. */
    void reset();

    /** @brief The promise to hand back to the script (requires EngineScope). */
    [[nodiscard]] Local<Promise> promise() const;

    /**
     * @brief Fulfil the promise with value.
     * @note  If value is a thenable, it is adopted per ECMA semantics
     *        (resolution continues through the engine job queue).
     */
    void resolve(Local<Value> const& value) const;

    /** @brief Reject the promise with reason. */
    void reject(Local<Value> const& reason) const;

private:
    using BackendImpl = internal::ImplType<jspp::PromiseResolver>::type;
    BackendImpl impl_;
};


} // namespace jspp
