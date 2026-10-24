#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Reference.h"

#include "jspp-backend/traits/TraitPromise.h"

namespace jspp {

/**
 * @brief C++ handle of an ECMA-262 PromiseCapability Record: holds a pending
 *        promise together with its resolve/reject capability.
 *
 * The promise handed to the script is a native engine promise, so await,
 * then-chaining, species behavior and microtask ordering are fully provided
 * by the engine (ECMA conformant).
 *
 * @note  Movable, not copyable. May be moved across threads; resolve()/reject()
 *        must run inside an EngineScope of the owning engine. QuickJS itself
 *        does not support multi-threaded runtimes; jspp enforces its own
 *        serial-access contract (engine mutex, plus JS_UpdateStackTop on every
 *        thread switch for stack overflow checks) and covers it with tests —
 *        access is strictly sequential, never concurrent, and this is not an
 *        upstream QuickJS guarantee. V8 serializes with v8::Locker.
 * @note  Lifetime contract is identical to Global<T>: a resolver must not
 *        outlive its Engine. Keep the engine alive until in-flight workers
 *        finish, or reset() the resolver beforehand.
 * @note  Settling twice is a silent no-op, per ECMA NewPromiseCapability
 *        (alreadyResolved flag).
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
