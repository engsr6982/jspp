#include "catch2/catch_test_macros.hpp"
#include "jspp/Jspp.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/PromiseResolver.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"

#include <optional>
#include <string>
#include <thread>

#ifdef JSPP_BACKEND_QUICKJS
#include "jspp-backend/queue/JobQueue.h"
#endif


struct PromiseTestFixture {
    std::unique_ptr<jspp::Engine> engine;
    PromiseTestFixture() { engine = std::make_unique<jspp::Engine>(); }
};

/**
 * Drive pending microtasks to completion.
 * - QuickJS: jspp pumps pending jobs into the engine JobQueue; drive it manually.
 * - V8: microtasks run at the engine's own checkpoints (after each script entry
 *   returns), so a trivial script entry triggers one.
 */
static void drainMicrotasks(jspp::Engine& engine) {
#ifdef JSPP_BACKEND_QUICKJS
    engine.pumpPendingJobs();
    while (engine.getJobQueue()->loopOnce()) {
    }
#else
    (void)engine.evalScript(jspp::String::newString("0"));
#endif
}

static jspp::Local<jspp::Value> evalGetDone(jspp::Engine& engine) {
    return engine.evalScript(jspp::String::newString("globalThis.done"));
}

/**
 * Installs a script-side .catch on `promise` that records the rejection reason
 * into globalThis.msg. Attach before draining microtasks so the rejection is
 * never observed as unhandled; read the message with observedMessage() after
 * draining. Independent of the C++ API under test.
 */
static void observeRejection(jspp::Engine& engine, jspp::Local<jspp::Promise> promise) {
    using namespace jspp;
    engine.globalThis().set(String::newString("q"), promise);
    engine.evalScript(String::newString(R"(
        globalThis.msg = 'unset';
        globalThis.q.catch(e => { globalThis.msg = (e && e.message) ? e.message : String(e); });
    )"));
}

static std::string observedMessage(jspp::Engine& engine) {
    return engine.evalScript(jspp::String::newString("globalThis.msg")).asString().getValue();
}

/**
 * Settles `resolver` with `value` and observes the outcome through a script
 * awaiter, so both the C++ settle path and the engine's adoption machinery are
 * exercised. Returns globalThis.done: "ok:<value>" or "err:<message>".
 */
static std::string settleAndAwait(jspp::Engine& engine, jspp::PromiseResolver& resolver, jspp::Local<jspp::Value> value) {
    using namespace jspp;
    engine.globalThis().set(String::newString("p"), resolver.promise());
    engine.evalScript(String::newString(R"(
        globalThis.done = 'unset';
        (async () => {
            try {
                globalThis.done = 'ok:' + (await globalThis.p);
            } catch (e) {
                globalThis.done = 'err:' + (e && e.message ? e.message : String(e));
            }
        })();
    )"));
    resolver.resolve(value);
    drainMicrotasks(engine);
    return evalGetDone(engine).asString().getValue();
}


// ---------------------------------------------------------------------------
// Resolver basics
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: resolve and await") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    std::optional<PromiseResolver> stash;
    auto getPromise = Function::newFunction([&stash](Arguments const& args) -> Local<Value> {
        stash = PromiseResolver::newResolver();
        return stash->promise();
    });
    engine->globalThis().set(String::newString("getPromise"), getPromise);

    engine->evalScript(String::newString(R"(
        globalThis.done = 'unset';
        (async () => {
            globalThis.done = await getPromise();
        })();
    )"));

    REQUIRE(stash.has_value());
    REQUIRE(stash->promise().state() == PromiseState::kPending);
    REQUIRE_THROWS_AS(stash->promise().result(), Exception);

    stash->resolve(Number::newNumber(42));

    drainMicrotasks(*engine);
    REQUIRE(evalGetDone(*engine).asNumber().getInt32() == 42);

    REQUIRE(stash->promise().state() == PromiseState::kFulfilled);
    REQUIRE(stash->promise().result().asNumber().getInt32() == 42);
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: reject and catch") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    std::optional<PromiseResolver> stash;
    auto getPromise = Function::newFunction([&stash](Arguments const& args) -> Local<Value> {
        stash = PromiseResolver::newResolver();
        return stash->promise();
    });
    engine->globalThis().set(String::newString("getPromise"), getPromise);

    engine->evalScript(String::newString(R"(
        globalThis.done = 'unset';
        (async () => {
            try {
                globalThis.done = await getPromise();
            } catch (e) {
                globalThis.done = 'caught:' + e;
            }
        })();
    )"));

    REQUIRE(stash.has_value());
    stash->reject(String::newString("boom"));

    drainMicrotasks(*engine);
    REQUIRE(evalGetDone(*engine).asString().getValue() == "caught:boom");

    REQUIRE(stash->promise().state() == PromiseState::kRejected);
    REQUIRE(stash->promise().result().asString().getValue() == "boom");
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: first settle wins (resolve first)") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    auto resolver = PromiseResolver::newResolver();
    resolver.resolve(Number::newNumber(1));
    REQUIRE_NOTHROW(resolver.reject(String::newString("late"))); // silently ignored
    REQUIRE_NOTHROW(resolver.resolve(Number::newNumber(2))); // silently ignored

    REQUIRE(resolver.promise().state() == PromiseState::kFulfilled);
    REQUIRE(resolver.promise().result().asNumber().getInt32() == 1);
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: first settle wins (reject first)") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    auto resolver = PromiseResolver::newResolver();
    resolver.reject(String::newString("first-rej"));
    REQUIRE_NOTHROW(resolver.resolve(Number::newNumber(1))); // silently ignored
    REQUIRE_NOTHROW(resolver.reject(String::newString("second-rej"))); // silently ignored

    REQUIRE(resolver.promise().state() == PromiseState::kRejected);
    REQUIRE(resolver.promise().result().asString().getValue() == "first-rej");
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: resolving with itself rejects with TypeError (ECMA self-resolution)") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    auto resolver = PromiseResolver::newResolver();
    auto done     = settleAndAwait(*engine, resolver, resolver.promise().asValue());

    REQUIRE(done.rfind("err:", 0) == 0); // rejected, message text is engine specific
    REQUIRE(resolver.promise().state() == PromiseState::kRejected);
}


// ---------------------------------------------------------------------------
// Thenable adoption (plain thenables, not native promises)
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: adopting plain thenables") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    auto resolver = PromiseResolver::newResolver();
    auto thenable = engine->evalScript(String::newString(R"(({ then(res) { res('thenable-ok'); } }))"));
    REQUIRE(settleAndAwait(*engine, resolver, thenable) == "ok:thenable-ok");
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: adopting a thenable that rejects") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    auto resolver = PromiseResolver::newResolver();
    auto thenable = engine->evalScript(String::newString(R"(({ then(res, rej) { rej('thenable-bad'); } }))"));
    auto done     = settleAndAwait(*engine, resolver, thenable);

    REQUIRE(done == "err:thenable-bad");
    REQUIRE(resolver.promise().state() == PromiseState::kRejected);
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: thenable settle functions are one-shot (ECMA alreadyResolved)") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    auto resolver = PromiseResolver::newResolver();
    auto thenable = engine->evalScript(
        String::newString(R"(({ then(res, rej) { res('first'); res('second'); rej('third'); } }))")
    );
    REQUIRE(settleAndAwait(*engine, resolver, thenable) == "ok:first");
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: thenable with a throwing then getter rejects") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    auto resolver = PromiseResolver::newResolver();
    auto thenable = engine->evalScript(String::newString(R"(
        (() => {
            const t = {};
            Object.defineProperty(t, 'then', { get() { throw new Error('getter-boom'); } });
            return t;
        })()
    )"));
    auto done = settleAndAwait(*engine, resolver, thenable);

    REQUIRE(done == "err:getter-boom");
    REQUIRE(resolver.promise().state() == PromiseState::kRejected);
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: thenable whose then call throws rejects") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    auto resolver = PromiseResolver::newResolver();
    auto thenable = engine->evalScript(String::newString(R"(({ then() { throw new Error('call-boom'); } }))"));
    auto done     = settleAndAwait(*engine, resolver, thenable);

    REQUIRE(done == "err:call-boom");
    REQUIRE(resolver.promise().state() == PromiseState::kRejected);
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: thenable resolving before throwing still fulfills") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    // res('first') settles the promise; the later throw must not override it.
    auto resolver = PromiseResolver::newResolver();
    auto thenable = engine->evalScript(
        String::newString(R"(({ then(res) { res('first'); throw new Error('after-resolve'); } }))")
    );
    REQUIRE(settleAndAwait(*engine, resolver, thenable) == "ok:first");
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: thenable getter read once, then called once with correct this") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    engine->evalScript(String::newString(R"(
        globalThis.stats = { getterCalls: 0, thenCalls: 0, thisIsThenable: false };
        globalThis.makeThenable = function() {
            const t = {};
            Object.defineProperty(t, 'then', {
                get() {
                    globalThis.stats.getterCalls++;
                    return function(res) {
                        globalThis.stats.thenCalls++;
                        globalThis.stats.thisIsThenable = (this === t);
                        res('once-ok');
                    };
                }
            });
            return t;
        };
    )"));

    auto resolver = PromiseResolver::newResolver();
    auto thenable = engine->evalScript(String::newString("globalThis.makeThenable()"));
    REQUIRE(settleAndAwait(*engine, resolver, thenable) == "ok:once-ok");

    auto getterCalls   = engine->evalScript(String::newString("globalThis.stats.getterCalls")).asNumber().getInt32();
    auto thenCalls     = engine->evalScript(String::newString("globalThis.stats.thenCalls")).asNumber().getInt32();
    auto thisIsThenable = engine->evalScript(String::newString("globalThis.stats.thisIsThenable")).asBoolean().getValue();
    REQUIRE(getterCalls == 1);
    REQUIRE(thenCalls == 1);
    REQUIRE(thisIsThenable);
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: adopting native promises") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    // fulfilled native promise
    {
        auto resolver = PromiseResolver::newResolver();
        auto inner     = engine->evalScript(String::newString("Promise.resolve('inner-value')"));
        REQUIRE(settleAndAwait(*engine, resolver, inner) == "ok:inner-value");
    }

    // already rejected native promise
    {
        auto resolver = PromiseResolver::newResolver();
        auto inner     = engine->evalScript(String::newString("Promise.reject('rejected-inner')"));
        auto done      = settleAndAwait(*engine, resolver, inner);
        REQUIRE(done == "err:rejected-inner");
        REQUIRE(resolver.promise().state() == PromiseState::kRejected);
    }
}


// ---------------------------------------------------------------------------
// Promise factories
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(PromiseTestFixture, "Promise: resolved / rejected factories") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    auto fulfilled = Promise::resolved(Number::newNumber(7));
    REQUIRE(fulfilled.state() == PromiseState::kFulfilled);
    REQUIRE(fulfilled.result().asNumber().getInt32() == 7);

    auto rejected = Promise::rejected(String::newString("nope"));
    REQUIRE(rejected.state() == PromiseState::kRejected);
    REQUIRE(rejected.result().asString().getValue() == "nope");

    auto resolver = PromiseResolver::newResolver();
    REQUIRE(settleAndAwait(*engine, resolver, fulfilled.asValue()) == "ok:7");
}


TEST_CASE_METHOD(PromiseTestFixture, "Promise::resolved always returns a new promise") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    // plain thenable is adopted
    {
        auto outer = Promise::resolved(engine->evalScript(
            String::newString(R"(({ then(res) { res('adopted'); } }))")
        ));

        engine->globalThis().set(String::newString("outer"), outer);
        engine->evalScript(String::newString(R"(
            globalThis.done = 'unset';
            (async () => { globalThis.done = await globalThis.outer; })();
        )"));

        drainMicrotasks(*engine);
        REQUIRE(evalGetDone(*engine).asString().getValue() == "adopted");
    }

    // native promise input yields a different promise with the same state
    // Contract: resolved() is not the Promise.resolve identity shortcut.
    {
        auto native  = engine->evalScript(String::newString("Promise.resolve('same')")).asPromise();
        auto wrapped = Promise::resolved(native.asValue());
        engine->globalThis().set(String::newString("a"), native);
        engine->globalThis().set(String::newString("b"), wrapped);
        REQUIRE(engine->evalScript(String::newString("globalThis.a !== globalThis.b")).asBoolean().getValue());

        auto resolver = PromiseResolver::newResolver();
        resolver.resolve(wrapped.asValue());
        drainMicrotasks(*engine);
        REQUIRE(resolver.promise().state() == PromiseState::kFulfilled);
        REQUIRE(resolver.promise().result().asString().getValue() == "same");
    }

    // already rejected native promise is adopted as rejected
    {
        auto outer = Promise::resolved(engine->evalScript(String::newString("Promise.reject('factory-rej')")));
        observeRejection(*engine, outer); // attached before draining
        drainMicrotasks(*engine);
        REQUIRE(outer.state() == PromiseState::kRejected);
        REQUIRE(outer.result().asString().getValue() == "factory-rej");
        REQUIRE(observedMessage(*engine) == "factory-rej");
    }
}


TEST_CASE_METHOD(PromiseTestFixture, "Bound function returning Local<Promise>") {
    using namespace jspp;

    static auto const meta = jspp::binding::defClass<void>("NativePromise")
                                 .func(
                                     "makeValue",
                                     [](int v) -> Local<Promise> {
                                         auto resolver = PromiseResolver::newResolver();
                                         auto promise  = resolver.promise();
                                         resolver.resolve(Number::newNumber(v));
                                         return promise;
                                     }
                                 )
                                 .build();

    EngineScope scope{engine.get()};
    engine->registerClass(meta);

    engine->evalScript(String::newString(R"(
        globalThis.done = 'unset';
        (async () => {
            globalThis.done = await NativePromise.makeValue(99);
        })();
    )"));

    drainMicrotasks(*engine);
    REQUIRE(evalGetDone(*engine).asNumber().getInt32() == 99);
}


// ---------------------------------------------------------------------------
// Local<Promise>::then
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(PromiseTestFixture, "Local<Promise>::then propagation") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    // Fulfilment handler receives the value; returning a plain value fulfils the chained promise.
    int  gotArg = 0;
    bool called = false;
    auto identity = Function::newFunction([&](Arguments const& args) -> Local<Value> {
        called = true;
        if (args.length() > 0 && args[0].isNumber()) {
            gotArg = args[0].asNumber().getInt32();
        }
        return Number::newNumber(gotArg * 2);
    });
    auto chained = Promise::resolved(Number::newNumber(5)).then(identity);
    drainMicrotasks(*engine);
    REQUIRE(called);
    REQUIRE(gotArg == 5);
    REQUIRE(chained.state() == PromiseState::kFulfilled);
    REQUIRE(chained.result().asNumber().getInt32() == 10);

    // Handler returning a promise: the chained promise adopts it.
    auto returnsPromise = Function::newFunction([](Arguments const& args) -> Local<Value> {
        return Promise::resolved(String::newString("from-inner"));
    });
    auto adopted = Promise::resolved(Number::newNumber(1)).then(returnsPromise);
    drainMicrotasks(*engine);
    REQUIRE(adopted.state() == PromiseState::kFulfilled);
    REQUIRE(adopted.result().asString().getValue() == "from-inner");

    // Handler throwing: the chained promise is rejected with the thrown error.
    // The observation handler is attached before draining so the rejection is
    // never unhandled.
    auto throwing = Function::newFunction([](Arguments const& args) -> Local<Value> {
        throw Exception{"cb-boom"};
    });
    auto failed = Promise::resolved(Number::newNumber(1)).then(throwing);
    observeRejection(*engine, failed);
    drainMicrotasks(*engine);
    REQUIRE(failed.state() == PromiseState::kRejected);
    REQUIRE(observedMessage(*engine) == "cb-boom");

    // Rejection handler recovering to a fulfilled chained promise.
    auto recovering = Function::newFunction([](Arguments const& args) -> Local<Value> {
        return String::newString("recovered");
    });
    auto recovered = Promise::rejected(String::newString("nope")).then(Function::newFunction([](Arguments const& args) -> Local<Value> {
        return {};
    }), recovering);
    drainMicrotasks(*engine);
    REQUIRE(recovered.state() == PromiseState::kFulfilled);
    REQUIRE(recovered.result().asString().getValue() == "recovered");
}


// ---------------------------------------------------------------------------
// Threads, emptiness, lifetime
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(PromiseTestFixture, "Local<Promise>::then omitted handler semantics") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    bool called = false;
    auto record = Function::newFunction([&](Arguments const& args) -> Local<Value> {
        called = true;
        return {};
    });

    // Omitted onRejected (single-argument then): the rejection passes through.
    auto forwarded = Promise::rejected(String::newString("passthrough-rej")).then(record);
    observeRejection(*engine, forwarded); // attached before draining
    drainMicrotasks(*engine);
    REQUIRE_FALSE(called);
    REQUIRE(forwarded.state() == PromiseState::kRejected);
    REQUIRE(forwarded.result().asString().getValue() == "passthrough-rej");
    REQUIRE(observedMessage(*engine) == "passthrough-rej");

    // Omitted onFulfilled (catchError): the value passes through.
    auto passed = Promise::resolved(Number::newNumber(11)).catchError(record);
    drainMicrotasks(*engine);
    REQUIRE_FALSE(called);
    REQUIRE(passed.state() == PromiseState::kFulfilled);
    REQUIRE(passed.result().asNumber().getInt32() == 11);

    // catchError recovery: a rejection handler returning a value fulfils the chained promise.
    auto recovering = Function::newFunction([](Arguments const& args) -> Local<Value> {
        return String::newString("recovered");
    });
    auto recovered = Promise::rejected(String::newString("nope")).catchError(recovering);
    drainMicrotasks(*engine);
    REQUIRE(recovered.state() == PromiseState::kFulfilled);
    REQUIRE(recovered.result().asString().getValue() == "recovered");
}


TEST_CASE_METHOD(PromiseTestFixture, "Calling script async functions from C++") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    // An async function call always returns a Promise, no matter what the
    // body returns or throws.

    // no explicit return fulfils with undefined
    {
        auto fn     = engine->evalScript(String::newString("(async () => {})")).asFunction();
        auto result = fn.call(Local<Value>{});
        REQUIRE(result.isPromise());
        auto promise = result.asPromise();
        drainMicrotasks(*engine);
        REQUIRE(promise.state() == PromiseState::kFulfilled);
        REQUIRE(promise.result().isUndefined());
    }

    // sync body value
    {
        auto fn     = engine->evalScript(String::newString("(async () => 42)")).asFunction();
        auto result = fn.call(Local<Value>{});
        REQUIRE(result.isPromise());
        auto promise = result.asPromise();
        drainMicrotasks(*engine);
        REQUIRE(promise.state() == PromiseState::kFulfilled);
        REQUIRE(promise.result().asNumber().getInt32() == 42);
    }

    // awaited body value
    {
        auto fn = engine->evalScript(String::newString("(async () => { await Promise.resolve(); return 42; })"))
                      .asFunction();
        auto result = fn.call(Local<Value>{});
        REQUIRE(result.isPromise());
        auto promise = result.asPromise();
        drainMicrotasks(*engine);
        REQUIRE(promise.state() == PromiseState::kFulfilled);
        REQUIRE(promise.result().asNumber().getInt32() == 42);
    }

    // body throwing rejects instead of propagating to the caller
    {
        auto fn = engine->evalScript(String::newString("(async () => { throw new Error('async-boom'); })")).asFunction();
        Local<Value> result;
        REQUIRE_NOTHROW(result = fn.call(Local<Value>{})); // the call itself must not throw
        REQUIRE(result.isPromise());
        auto promise = result.asPromise();
        observeRejection(*engine, promise); // attach before draining
        drainMicrotasks(*engine);
        REQUIRE(promise.state() == PromiseState::kRejected);
        REQUIRE(observedMessage(*engine) == "async-boom");
    }
}


// ---------------------------------------------------------------------------
// Type recognition
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(PromiseTestFixture, "Local<Value>::isPromise / asPromise type edges") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    auto promise = engine->evalScript(String::newString("Promise.resolve(1)"));
    REQUIRE(promise.isPromise());
    REQUIRE(promise.kind() == ValueKind::kPromise);
    REQUIRE(promise.asPromise().state() == PromiseState::kFulfilled);

    // Plain objects, thenables and non-objects are not native promises.
    for (auto const* expr : {"({})", "({ then() {} })", "null", "undefined", "42"}) {
        CAPTURE(expr);
        auto value = engine->evalScript(String::newString(expr));
        REQUIRE_FALSE(value.isPromise());
        REQUIRE(value.kind() != ValueKind::kPromise);
        REQUIRE_THROWS_AS(value.asPromise(), Exception);
    }

    // A thenable is a plain object for kind().
    REQUIRE(engine->evalScript(String::newString("({ then() {} })")).kind() == ValueKind::kObject);
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: cross-thread settle") {
    using namespace jspp;

    {
        EngineScope scope{engine.get()};
        engine->evalScript(String::newString(R"(
            globalThis.done = [];
            globalThis.awaitP = function(p, tag) {
                p.then(v => { globalThis.done.push(tag + ':' + v); },
                       e => { globalThis.done.push(tag + ':err:' + e); });
            };
        )"));
    }

    // Alternate main/worker threads for several rounds: each round moves one
    // resolver to a worker thread, settles it there and reads the result back
    // on the main thread. QuickJS runtimes are single threaded; EngineScope
    // serializes access (engine mutex + JS_UpdateStackTop on scope entry),
    // which is what these thread switches exercise.
    for (int round = 0; round < 3; ++round) {
        PromiseResolver resolver;
        {
            EngineScope scope{engine.get()};
            resolver = PromiseResolver::newResolver();
            auto name = "p" + std::to_string(round);
            engine->globalThis().set(String::newString(name), resolver.promise());
            engine->evalScript(String::newString(
                "globalThis.awaitP(globalThis." + name + ", '" + std::to_string(round) + "');"
            ));
        } // release the scope so the worker can enter

        auto worker = std::thread([engine = engine.get(), &resolver, round] {
            EngineScope scope{engine};
            resolver.resolve(String::newString("from-thread-" + std::to_string(round)));
        });
        worker.join();

        EngineScope scope{engine.get()};
        drainMicrotasks(*engine);
    }

    EngineScope scope{engine.get()};
    auto done = engine->evalScript(String::newString("globalThis.done.join(',')")).asString().getValue();
    REQUIRE(done == "0:from-thread-0,1:from-thread-1,2:from-thread-2");
}


TEST_CASE_METHOD(PromiseTestFixture, "PromiseResolver: empty and reset semantics") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    PromiseResolver empty;
    REQUIRE(empty.isEmpty());
    REQUIRE_THROWS_AS(empty.promise(), Exception);
    REQUIRE_THROWS_AS(empty.resolve(Local<Value>{}), Exception);
    REQUIRE_THROWS_AS(empty.reject(Local<Value>{}), Exception);

    auto resolver = PromiseResolver::newResolver();
    REQUIRE_FALSE(resolver.isEmpty());
    auto promise = resolver.promise();
    resolver.reset();
    REQUIRE(resolver.isEmpty());
    REQUIRE_THROWS_AS(resolver.promise(), Exception);

    // The promise itself stays pending and keeps working on the script side.
    REQUIRE(promise.state() == PromiseState::kPending);
    REQUIRE(promise.asValue().isPromise());
}


TEST_CASE_METHOD(PromiseTestFixture, "Unhandled rejection does not crash the engine") {
    using namespace jspp;
    {
        EngineScope scope{engine.get()};
        auto resolver = PromiseResolver::newResolver();
        engine->globalThis().set(String::newString("p"), resolver.promise());
        resolver.reject(String::newString("nobody listens"));
        // The resolver is destroyed here, before the engine (contract: a
        // resolver must not outlive its engine, so the reverse order,
        // destroying the engine while a resolver is still alive, is UB by
        // contract and intentionally not tested).
    }
    engine.reset();
    SUCCEED();
}
