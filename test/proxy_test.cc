#include "catch2/catch_test_macros.hpp"
#include "jspp/Jspp.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"

#include <string>

#ifdef JSPP_BACKEND_QUICKJS
#include "jspp-backend/queue/JobQueue.h"
#endif


struct ProxyTestFixture {
    std::unique_ptr<jspp::Engine> engine;
    ProxyTestFixture() { engine = std::make_unique<jspp::Engine>(); }
};

static void drainMicrotasks(jspp::Engine& engine) {
#ifdef JSPP_BACKEND_QUICKJS
    engine.pumpPendingJobs();
    while (engine.getJobQueue()->loopOnce()) {
    }
#else
    (void)engine.evalScript(jspp::String::newString("0"));
#endif
}


// ---------------------------------------------------------------------------
// Traps are synchronous internal method calls
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(ProxyTestFixture, "Proxy: C++ handler traps are synchronous") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    bool        getCalled = false, setCalled = false, hasCalled = false;
    std::string gotKey, setKey;
    int         setValue = 0;

    auto handler = Object::newObject();
    handler.set(
        String::newString("get"),
        Function::newFunction([&](Arguments const& a) -> Local<Value> {
            getCalled = true; // (target, key, receiver)
            gotKey    = a[1].asString().getValue();
            return a[0].asObject().get(a[1].asString()); // forward to target
        })
    );
    handler.set(
        String::newString("set"),
        Function::newFunction([&](Arguments const& a) -> Local<Value> {
            setCalled = true;
            setKey    = a[1].asString().getValue();
            setValue  = a[2].asNumber().getInt32();
            a[0].asObject().set(a[1].asString(), a[2]);
            return Boolean::newBoolean(true);
        })
    );
    handler.set(
        String::newString("has"),
        Function::newFunction([&](Arguments const& a) -> Local<Value> {
            hasCalled = true;
            return Boolean::newBoolean(true);
        })
    );

    auto target = Object::newObject();
    target.set(String::newString("v"), Number::newNumber(7));

    auto proxy = Proxy::newProxy(target, handler);
    auto obj   = proxy.asValue().asObject();

    // Proxy operations do not become asynchronous: traps must have run by the
    // time the operation returns; no microtask draining below.
    REQUIRE(obj.get(String::newString("v")).asNumber().getInt32() == 7);
    REQUIRE(getCalled);
    REQUIRE(gotKey == "v");

    obj.set(String::newString("v"), Number::newNumber(8));
    REQUIRE(setCalled);
    REQUIRE(setKey == "v");
    REQUIRE(setValue == 8);
    REQUIRE(target.get(String::newString("v")).asNumber().getInt32() == 8); // trap forwarded the write

    (void)obj.has(String::newString("v"));
    REQUIRE(hasCalled);
}


// ---------------------------------------------------------------------------
// Introspection (jspp host extension): target / handler / revoked
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(ProxyTestFixture, "Proxy: introspection of target, handler and revoked proxies") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    engine->evalScript(String::newString(R"(
        globalThis.t  = { marker: 1 };
        globalThis.h  = {};
        globalThis.px = new Proxy(globalThis.t, globalThis.h);
    )"));
    auto proxy = engine->evalScript(String::newString("globalThis.px")).asProxy();
    REQUIRE(proxy.asValue().isProxy());

    engine->globalThis().set(String::newString("tt"), proxy.target());
    engine->globalThis().set(String::newString("hh"), proxy.handler());
    REQUIRE(engine->evalScript(String::newString("globalThis.tt === globalThis.t")).asBoolean().getValue());
    REQUIRE(engine->evalScript(String::newString("globalThis.hh === globalThis.h")).asBoolean().getValue());

    // A script-side revocable proxy: holding and introspecting it stays valid,
    // with target/handler becoming null once revoked.
    engine->evalScript(String::newString("globalThis.rv = Proxy.revocable({}, {}); globalThis.rv.revoke();"));
    auto revoked = engine->evalScript(String::newString("globalThis.rv.proxy")).asProxy();
    REQUIRE(revoked.target().isNull());
    REQUIRE(revoked.handler().isNull());
}


// ---------------------------------------------------------------------------
// Type recognition
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(ProxyTestFixture, "Proxy: type recognition") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    // Every proxy is recognized as a proxy, regardless of its target kind.
    for (auto const* expr : {"new Proxy({}, {})", "new Proxy([], {})", "new Proxy(function(){}, {})",
                             "new Proxy(Promise.resolve(1), {})"}) {
        CAPTURE(expr);
        auto value = engine->evalScript(String::newString(expr));
        REQUIRE(value.isProxy());
        REQUIRE(value.kind() == ValueKind::kProxy);
        REQUIRE(value.asProxy().target().isObject());
    }

    // Object-target proxy keeps object capabilities.
    auto objectProxy = engine->evalScript(String::newString("new Proxy({}, {})"));
    REQUIRE(objectProxy.isObject());
    REQUIRE_NOTHROW(objectProxy.asObject());

    // Function-target proxy: callable, and still an object (ECMA: functions
    // are objects; ProxyCreate grants callability from the target).
    auto fnProxy = engine->evalScript(String::newString("new Proxy(function (x) { return x + 1; }, {})"));
    REQUIRE(fnProxy.isObject());
    REQUIRE(fnProxy.isFunction());
    auto callResult = fnProxy.asFunction().call(Local<Value>{}, {Number::newNumber(5)});
    REQUIRE(callResult.asNumber().getInt32() == 6);

    // The same contract holds for plain functions.
    auto plainFn = engine->evalScript(String::newString("(function () {})"));
    REQUIRE(plainFn.isObject());
    REQUIRE(plainFn.isFunction());

    // Non-proxies are unaffected.
    for (auto const* expr : {"({})", "({ then() {} })", "null", "undefined", "42", "Promise.resolve(1)"}) {
        CAPTURE(expr);
        auto value = engine->evalScript(String::newString(expr));
        REQUIRE_FALSE(value.isProxy());
        REQUIRE(value.kind() != ValueKind::kProxy);
        REQUIRE_THROWS_AS(value.asProxy(), Exception);
    }
}


TEST_CASE_METHOD(ProxyTestFixture, "Proxy: isArray is an engine-native type query, not ECMA IsArray") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    engine->evalScript(String::newString("globalThis.arrProxy = new Proxy([], {});"));
    auto value = engine->evalScript(String::newString("globalThis.arrProxy"));

    // Current contract: engine-native type check; both backends report false
    // for proxies, while script-side Array.isArray (ECMA IsArray) punches
    // through to the target. Upgrading isArray() semantics is a separate
    // contract question, deliberately out of scope here.
    REQUIRE_FALSE(value.isArray());
    REQUIRE(engine->evalScript(String::newString("Array.isArray(globalThis.arrProxy)")).asBoolean().getValue());
}


// ---------------------------------------------------------------------------
// Proxy invariant enforcement.
//
// The engine must reject handler results that violate the target's own
// invariants: a get trap reporting a different value for a non-configurable,
// non-writable data property, and a set trap claiming success for such a
// property. Each path is checked twice: inside a script (the thrown value
// must be a TypeError) and through the C++ property API (the propagated
// jspp::Exception must carry that same engine error).
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(ProxyTestFixture, "Proxy: invariant violations throw TypeError") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    engine->globalThis().set(
        String::newString("makeFixedTarget"),
        Function::newFunction([](Arguments const&) -> Local<Value> {
            auto target = Object::newObject();
            (void)target.defineOwnProperty(
                String::newString("fixed"),
                Number::newNumber(1),
                PropertyAttribute::ReadOnly | PropertyAttribute::DontEnum | PropertyAttribute::DontDelete
            );
            return target;
        })
    );

    // get trap lying about a non-configurable, non-writable data property
    engine->evalScript(String::newString(R"(
        globalThis.getLiar = new Proxy(globalThis.makeFixedTarget(), {
            get() { return 2; }
        });
        try {
            void globalThis.getLiar.fixed;
            globalThis.getOk = 'no-throw';
        } catch (e) {
            globalThis.getOk = (e instanceof TypeError) ? 'TypeError' : 'other';
        }
    )"));
    REQUIRE(engine->evalScript(String::newString("globalThis.getOk")).asString().getValue() == "TypeError");

    {
        // the same read through the C++ API: the propagated jspp::Exception
        // must carry the engine's invariant TypeError (classified by routing
        // the underlying error object back into a script check)
        auto  proxy = engine->evalScript(String::newString("globalThis.getLiar")).asProxy();
        bool  threw = false;
        try {
            (void)proxy.asValue().asObject().get(String::newString("fixed"));
        } catch (Exception const& e) {
            threw = true;
            engine->globalThis().set(String::newString("lastError"), e.exception());
        }
        REQUIRE(threw);
        REQUIRE(engine->evalScript(String::newString("globalThis.lastError instanceof TypeError")).asBoolean().getValue());
    }

    // set trap claiming a successful write to the same kind of property
    engine->evalScript(String::newString(R"(
        globalThis.setLiar = new Proxy(globalThis.makeFixedTarget(), {
            set() { return true; }
        });
        try {
            globalThis.setLiar.fixed = 2;
            globalThis.setOk = 'no-throw';
        } catch (e) {
            globalThis.setOk = (e instanceof TypeError) ? 'TypeError' : 'other';
        }
    )"));
    REQUIRE(engine->evalScript(String::newString("globalThis.setOk")).asString().getValue() == "TypeError");

    {
        auto proxy = engine->evalScript(String::newString("globalThis.setLiar")).asProxy();
        bool threw = false;
        try {
            proxy.asValue().asObject().set(String::newString("fixed"), Number::newNumber(2));
        } catch (Exception const& e) {
            threw = true;
            engine->globalThis().set(String::newString("lastError"), e.exception());
        }
        REQUIRE(threw);
        REQUIRE(engine->evalScript(String::newString("globalThis.lastError instanceof TypeError")).asBoolean().getValue());
    }
}


// ---------------------------------------------------------------------------
// Construction validation
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(ProxyTestFixture, "Proxy: non-object constructor arguments throw TypeError at script level") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    // The C++ API takes Local<Object> so this cannot happen through newProxy();
    // this pins the engine TypeError path and jspp's exception conversion.
    REQUIRE_THROWS_AS(engine->evalScript(String::newString("new Proxy(1, {})")), Exception);
    REQUIRE_THROWS_AS(engine->evalScript(String::newString("new Proxy({}, null)")), Exception);
}


// ---------------------------------------------------------------------------
// Native instances behind a proxy (internal slots do not propagate)
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Promise x Proxy
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(ProxyTestFixture, "Proxy with promises") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    // (a) a plain thenable wrapped in a proxy is adopted through the get trap
    engine->evalScript(String::newString(R"(
        globalThis.done = 'unset';
        globalThis.thenableProxy = new Proxy({ then(res) { res('via-proxy-thenable'); } }, {});
        (async () => { globalThis.done = await globalThis.thenableProxy; })();
    )"));
    drainMicrotasks(*engine);
    REQUIRE(engine->evalScript(String::newString("globalThis.done")).asString().getValue() == "via-proxy-thenable");

    // (b) a native promise behind a transparent proxy: no promise internal
    //     slots propagate, so await rejects with TypeError
    auto promiseProxy = engine->evalScript(String::newString("new Proxy(Promise.resolve(42), {})"));
    REQUIRE(promiseProxy.isProxy());
    REQUIRE_FALSE(promiseProxy.isPromise());
    engine->globalThis().set(String::newString("px2"), promiseProxy);
    engine->evalScript(String::newString(R"(
        globalThis.done = 'unset';
        (async () => {
            try { globalThis.done = 'ok:' + (await globalThis.px2); }
            catch (e) { globalThis.done = 'err:' + (e instanceof TypeError); }
        })();
    )"));
    drainMicrotasks(*engine);
    REQUIRE(engine->evalScript(String::newString("globalThis.done")).asString().getValue() == "err:true");

    // (c) a custom get trap binding then back to the target restores await,
    //     a handler-provided compatibility, not an automatic proxy capability
    engine->evalScript(String::newString(R"(
        globalThis.done = 'unset';
        const promise = Promise.resolve(42);
        globalThis.boundThenProxy = new Proxy(promise, {
            get(target, key) {
                const value = Reflect.get(target, key, target);
                return key === "then" && typeof value === "function"
                    ? value.bind(target)
                    : value;
            }
        });
        (async () => { globalThis.done = await globalThis.boundThenProxy; })();
    )"));
    drainMicrotasks(*engine);
    REQUIRE(engine->evalScript(String::newString("globalThis.done")).asNumber().getInt32() == 42);
}


// ---------------------------------------------------------------------------
// Bidirectional interop: real cross-boundary calls
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(ProxyTestFixture, "Proxy interop: native traps driven from script") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    bool        getCalled = false, setCalled = false, hasChecked = false;
    std::string gotKey;
    std::string thrownMessage;

    auto handler = Object::newObject();
    handler.set(
        String::newString("get"),
        Function::newFunction([&](Arguments const& a) -> Local<Value> {
            getCalled = true;
            gotKey    = a[1].asString().getValue();
            if (gotKey == "boom") {
                throw Exception{"trap-boom"}; // must propagate to the script side
            }
            return String::newString("native-value");
        })
    );
    handler.set(
        String::newString("set"),
        Function::newFunction([&](Arguments const& /*a*/) -> Local<Value> {
            setCalled = true;
            return Boolean::newBoolean(true);
        })
    );
    handler.set(
        String::newString("has"),
        Function::newFunction([&](Arguments const& a) -> Local<Value> {
            hasChecked = true;
            return Boolean::newBoolean(a[1].asString().getValue() == "answer");
        })
    );

    auto proxy = Proxy::newProxy(Object::newObject(), handler);
    engine->globalThis().set(String::newString("px"), proxy);

    // Script drives the proxy; the C++ traps fire during these operations.
    engine->evalScript(String::newString(R"(
        globalThis.read = String(globalThis.px.answer);
        globalThis.px.answer = 5;
        globalThis.present = ('answer' in globalThis.px);
        try { void globalThis.px.boom; globalThis.err = 'none'; }
        catch (e) { globalThis.err = String(e); }
    )"));

    REQUIRE(getCalled);
    REQUIRE(gotKey == "boom"); // last get
    REQUIRE(setCalled);
    REQUIRE(hasChecked);
    REQUIRE(engine->evalScript(String::newString("globalThis.read")).asString().getValue() == "native-value");
    REQUIRE(engine->evalScript(String::newString("globalThis.present")).asBoolean().getValue());
    auto err = engine->evalScript(String::newString("globalThis.err")).asString().getValue();
    REQUIRE(err.find("trap-boom") != std::string::npos); // C++ exception reached the script
}


TEST_CASE_METHOD(ProxyTestFixture, "Proxy interop: script traps driven from C++") {
    using namespace jspp;
    EngineScope scope{engine.get()};

    engine->evalScript(String::newString(R"(
        globalThis.px = new Proxy({ v: 1 }, {
            get(target, key, receiver) {
                if (key === 'boom') { throw new Error('script-trap-boom'); }
                return target[key];
            },
            set(target, key, value) {
                target[key] = value;
                return true;
            }
        });
    )"));

    auto proxy = engine->evalScript(String::newString("globalThis.px")).asProxy();
    auto obj   = proxy.asValue().asObject();

    // C++ drives the proxy; the script traps fire during these operations.
    REQUIRE(obj.get(String::newString("v")).asNumber().getInt32() == 1);

    obj.set(String::newString("v"), Number::newNumber(9));
    REQUIRE(obj.get(String::newString("v")).asNumber().getInt32() == 9); // trap persisted the write

    // A trap-thrown script exception surfaces as jspp::Exception at the caller.
    bool threw = false;
    try {
        (void)obj.get(String::newString("boom"));
    } catch (Exception const& e) {
        threw = true;
        REQUIRE(std::string(e.what()).find("script-trap-boom") != std::string::npos);
    }
    REQUIRE(threw);
}
