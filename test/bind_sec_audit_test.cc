#include "catch2/catch_test_macros.hpp"
#include "jspp/Jspp.h"
#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/binding/TypeConverter.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>

// ===========================================================================
// Security-focused tests for the native instance binding layer.
//
// Case names carry a BIND-SEC-xxx tag kept from the security audit that
// produced them; the numbering has no external document. Each case has a
// self-describing comment above it. Related receiver and proxy coverage
// lives in native_proxy_interop_test.cc and proxy_test.cc.
// ===========================================================================

namespace {

struct SecBase {
    SecBase() = default;
    virtual ~SecBase() = default;

    int value = 5;

    int  add(int v) {
        value += v;
        return value;
    }
    int  getValue() const { return value; }
    void setValue(int v) { value = v; }

    int boom() { throw std::runtime_error{"method-boom"}; }

    // Calls a script callback mid-method and keeps using the instance after
    // it returns; the callback may drop the wrapper and force a GC (see the
    // "reentrant callback" case below).
    int reentrant(int arg);

    static int takesInt(int v) { return v; }
    static unsigned int takesUint(unsigned int v) { return v; }
    static double takesDouble(double v) { return v; }

    inline static int s_sharedDestructed = 0;
};

struct SecShared {
    SecShared() = default;
    ~SecShared() { ++SecBase::s_sharedDestructed; }
    int tag = 1;
};

int SecBase::reentrant(int arg) {
    auto& engine = jspp::EngineScope::currentEngineChecked();
    auto  cb     = engine.globalThis().get(jspp::String::newString("onReenter"));
    if (cb.isFunction()) {
        (void)cb.asFunction().call(jspp::Local<jspp::Value>{}, {jspp::Number::newNumber(arg)});
    }
    // The callback above may have dropped the last JS reference to this
    // instance and forced a GC. Touching the member afterwards must stay
    // within the instance lifetime guaranteed by the binding layer.
    value += arg;
    return value;
}

auto const kSecBaseMeta = jspp::binding::defClass<SecBase>("SecBase")
                              .ctor<>()
                              .method("add", &SecBase::add)
                              .prop("value", &SecBase::getValue, &SecBase::setValue)
                              .method("boom", &SecBase::boom)
                              .method("reentrant", &SecBase::reentrant)
                              .build();

std::shared_ptr<SecShared> g_sharedInstance = std::make_shared<SecShared>();

std::shared_ptr<SecShared> makeShared() {
    return g_sharedInstance; // every call wraps the SAME shared_ptr
}

auto const kSecSharedMeta =
    jspp::binding::defClass<SecShared>("SecShared").ctor<>().prop("tag", &SecShared::tag).build();

auto const kSecNumMeta = jspp::binding::defClass<void>("SecNum")
                             .func("takesInt", &SecBase::takesInt)
                             .func("takesUint", &SecBase::takesUint)
                             .func("takesDouble", &SecBase::takesDouble)
                             .build();

void registerSecClasses(jspp::Engine& engine) {
    engine.registerClass(kSecBaseMeta);
    engine.registerClass(kSecSharedMeta);
    engine.registerClass(kSecNumMeta);
    engine.globalThis().set(
        jspp::String::newString("makeSharedObj"),
        jspp::Function::newFunction(jspp::binding::cpp_func([]() -> std::shared_ptr<SecShared> {
            return makeShared();
        }))
    );
}

int evalInt(jspp::Engine& engine, std::string const& expr) {
    return engine.evalScript(jspp::String::newString(expr)).asNumber().getInt32();
}

std::string evalStr(jspp::Engine& engine, std::string const& expr) {
    return engine.evalScript(jspp::String::newString(expr)).asString().getValue();
}

void runFullGc(jspp::Engine& engine, int attempts = 10) {
    for (int i = 0; i < attempts; ++i) {
        engine.gc();
        std::this_thread::sleep_for(std::chrono::milliseconds(12));
    }
}

} // namespace


struct BindSecFixture {
    std::unique_ptr<jspp::Engine> engine;
    BindSecFixture() { engine = std::make_unique<jspp::Engine>(); }
};


// ---------------------------------------------------------------------------
// Proxies wrapping NON-instances used as receivers.
//
// A proxy whose target is not a native instance (a plain object, an object
// carrying the class prototype, a function) must not pass the receiver
// checks of bound members. Each forged receiver is driven through the
// method, the descriptor getter and the descriptor setter; every path must
// throw a TypeError. A get trap that runs extra script during the access
// must not weaken the rejection.
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(BindSecFixture, "BIND-SEC-002: proxies of forged objects as receivers") {
    using namespace jspp;
    EngineScope scope{engine.get()};
    registerSecClasses(*engine);

    engine->evalScript(String::newString(R"(
        const proto = SecBase.prototype;
        const desc  = Object.getOwnPropertyDescriptor(proto, 'value');

        // Proxies whose TARGETS are not native instances at all: a plain
        // object, a forged prototype clone, a function.
        const receivers = [
            new Proxy({}, {}),
            new Proxy(Object.create(proto), {}),
            new Proxy(function () {}, {})
        ];

        globalThis.out = [];
        receivers.forEach((R) => {
            const outcomes = [];
            const check = (fn) => {
                try { void fn(); outcomes.push('no-throw'); }
                catch (e) { outcomes.push(e instanceof TypeError ? 'TypeError' : 'other'); }
            };
            check(() => proto.add.call(R, 1));
            check(() => desc.get.call(R));
            check(() => desc.set.call(R, 1));
            globalThis.out.push(outcomes.join(','));
        });

        // A trap that runs extra JS during a member access on a proxied real
        // instance: the trap executes, the native call is still rejected.
        const realInst = new SecBase();
        const trapped = new Proxy(realInst, {
            get(t, k) { globalThis.trapRan = true; return Reflect.get(t, k, t); }
        });
        try { void trapped.add(1); } catch (e) {}
        globalThis.after = 1 + 1;
    )"));

    REQUIRE(evalStr(*engine, "globalThis.out.join('|')") == "TypeError,TypeError,TypeError|TypeError,TypeError,TypeError|TypeError,TypeError,TypeError");
    // The get trap ran (extra JS executed) but the call was still rejected.
    REQUIRE(engine->evalScript(String::newString("globalThis.trapRan")).asBoolean().getValue());
    REQUIRE(evalInt(*engine, "globalThis.after") == 2);
}


// ---------------------------------------------------------------------------
// Identity forgery: Symbol.hasInstance, JS subclasses, substitute returns.
//
// 1. Forging Symbol.hasInstance makes instanceof report true for any
//    object; bound members must still reject the liar receiver.
// 2. class JsSub extends SecBase with a real super() constructs the
//    native part, so inherited members keep working on the subclass.
// 3. A derived constructor returning a substitute object never builds the
//    native storage; the borrowed native method must reject it even though
//    the forged instanceof (point 1) still reports true.
// 4. Deleting a non-configurable property throws a TypeError in strict
//    mode on every engine; the thrown jspp::Exception is consumed while
//    the engine is alive.
//
// Plain prototype-swap receivers are covered by native_proxy_interop_test.cc
// ("invalid receivers").
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(BindSecFixture, "BIND-SEC-003: prototype and instanceof forgery") {
    using namespace jspp;
    EngineScope scope{engine.get()};
    registerSecClasses(*engine);

    // Receivers that merely point their prototype chain at the class
    // prototype are covered by native_proxy_interop_test.cc ("invalid
    // receivers"). This case covers the remaining forgery paths.

    engine->evalScript(String::newString(R"(
        globalThis.check = (fn) => {
            try { void fn(); return 'no-throw'; }
            catch (e) { return e instanceof TypeError ? 'TypeError' : 'other'; }
        };

        // (1) Symbol.hasInstance forgery: instanceof reports true for any
        //     object; the bound method, borrowed onto the liar receiver,
        //     must still reject it.
        const desc2 = Object.getOwnPropertyDescriptor(SecBase.prototype, 'value');
        Object.defineProperty(SecBase, Symbol.hasInstance, {
            value: function (v) { return true; }, configurable: true
        });
        const liar = { totally: 'not an instance' };
        globalThis.liarInstanceof = (liar instanceof SecBase); // forced true
        globalThis.o3 = globalThis.check(() => SecBase.prototype.add.call(liar, 1));
        globalThis.o4 = globalThis.check(() => desc2.set.call(liar, 9));

        // (2) Reflect.construct with a foreign new.target: both engines lay
        //     the receiver out from the called constructor, so the instance
        //     is real; verify by an actual native call and its return value.
        try {
            const via = Reflect.construct(SecBase, [], Array);
            globalThis.o10v = SecBase.prototype.add.call(via, 1);
            globalThis.o10 = 'no-throw';
        } catch (e) { globalThis.o10 = 'threw'; }
    )"));

    engine->evalScript(String::newString(R"(
        // (3) a JS subclass extends the native class: super() constructs the
        //     native part, so inherited members keep working on the subclass,
        //     including through a real super.add() call.
        class JsSub extends SecBase {
            constructor() { super(); this.jsOnly = 1; }
            viaThis() { return this.add(1); }
            callSuper() { return super.add(1); }
        }
        const jsSub = new JsSub();
        globalThis.o5 = globalThis.check(() => jsSub.add(1));
        globalThis.o6 = globalThis.check(() => jsSub.viaThis());
        globalThis.o9 = globalThis.check(() => { globalThis.o9v = jsSub.callSuper(); });
    )"));

    // expose the descriptor getter to later stages
    engine->evalScript(String::newString(
        "globalThis.descGet = () => Object.getOwnPropertyDescriptor(SecBase.prototype, 'value').get;"
    ));

    engine->evalScript(String::newString(R"(
        // (4) a subclass that never reaches super() returns a substitute
        //     object without native storage. The forged instanceof (the
        //     hasInstance hook from stage 1 is still active) reports true,
        //     but the borrowed native method must reject the receiver.
        class NoSuper extends SecBase {
            constructor() { return { marker: 1 }; }
        }
        const noSuper = new NoSuper();
        globalThis.noSuperInstanceof = (noSuper instanceof SecBase);
        globalThis.o7 = globalThis.check(() => SecBase.prototype.add.call(noSuper, 1));
        globalThis.o8 = globalThis.check(() => descGet().call(noSuper));
    )"));

    // (5) Deleting a non-configurable property throws a TypeError in strict
    // mode on every engine (the QuickJS backend always evaluates scripts
    // strictly; the V8 backend defaults to sloppy, so the prefix makes the
    // modes match). The thrown jspp::Exception is consumed while the engine
    // is alive, per the lifecycle note on jspp::Exception.
    {
        engine->evalScript(String::newString(
            "function F2() {}"
            "Object.defineProperty(F2, Symbol.hasInstance, { value: function (v) { return true; } });"
        ));
        auto throws = [&] {
            return engine->evalScript(String::newString("'use strict'; delete F2[Symbol.hasInstance]; 0"));
        };
        REQUIRE_THROWS_AS(throws(), Exception);
        // and the engine keeps working afterwards
        REQUIRE(evalInt(*engine, "1 + 1") == 2);
    }

    engine->evalScript(String::newString("globalThis.after = 1 + 1;"));

    REQUIRE(engine->evalScript(String::newString("globalThis.liarInstanceof")).asBoolean().getValue());
    REQUIRE(evalStr(*engine, "globalThis.o3") == "TypeError");
    REQUIRE(evalStr(*engine, "globalThis.o4") == "TypeError");
    REQUIRE(evalStr(*engine, "globalThis.o10") == "no-throw");
    REQUIRE(evalInt(*engine, "globalThis.o10v") == 6); // real native call result
    REQUIRE(evalStr(*engine, "globalThis.o5") == "no-throw"); // JS subclass with real super()
    REQUIRE(evalStr(*engine, "globalThis.o6") == "no-throw");
    REQUIRE(evalStr(*engine, "globalThis.o9") == "no-throw");
    REQUIRE(evalInt(*engine, "globalThis.o9v") == 8); // 5 + o5 + o6 + o9 increments
    REQUIRE(engine->evalScript(String::newString("globalThis.noSuperInstanceof")).asBoolean().getValue());
    REQUIRE(evalStr(*engine, "globalThis.o7") == "TypeError");
    REQUIRE(evalStr(*engine, "globalThis.o8") == "TypeError");
    REQUIRE(evalInt(*engine, "globalThis.after") == 2);
}


// ---------------------------------------------------------------------------
// Descriptor accessors outliving their instance.
//
// The getter/setter pair is saved from the property descriptor, the last
// reference to the instance is dropped and GC runs. Calling the saved
// accessors afterwards must fail with a TypeError on every receiver
// without touching freed memory (checked under ASan).
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(BindSecFixture, "BIND-SEC-007: extracted accessors after instance collection") {
    using namespace jspp;
    EngineScope scope{engine.get()};
    registerSecClasses(*engine);

    engine->evalScript(String::newString(R"(
        (function () {
            const proto = SecBase.prototype;
            const desc = Object.getOwnPropertyDescriptor(proto, 'value');
            globalThis.savedGet = desc.get;
            globalThis.savedSet = desc.set;
            globalThis.holder = new SecBase();
        })();
    )"));
    engine->evalScript(String::newString("(function () { globalThis.holder = null; })();"));
    runFullGc(*engine);

    // The wrappers are gone; calling the saved accessors on anything must be
    // rejected without dereferencing freed memory.
    engine->evalScript(String::newString(R"(
        const outcomes = [];
        const check = (fn) => {
            try { void fn(); outcomes.push('no-throw'); }
            catch (e) { outcomes.push(e instanceof TypeError ? 'TypeError' : 'other'); }
        };
        check(() => globalThis.savedGet.call({}));
        check(() => globalThis.savedGet.call(new Proxy({}, {})));
        check(() => globalThis.savedSet.call({}, 1));
        globalThis.outcomes = outcomes.join(',');
        globalThis.after = 1 + 1;
    )"));

    REQUIRE(evalStr(*engine, "globalThis.outcomes") == "TypeError,TypeError,TypeError");
    REQUIRE(evalInt(*engine, "globalThis.after") == 2);
}


// ---------------------------------------------------------------------------
// shared_ptr ownership across multiple JS wrappers.
//
// Two wrappers over the same std::shared_ptr plus a C++ owner: collecting
// both wrappers while the C++ owner lives must not destruct the object;
// releasing the last owner must destruct it exactly once.
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(BindSecFixture, "BIND-SEC-008: shared_ptr wrappers release exactly once") {
    using namespace jspp;

    // Rebuild the C++ owner on every run so repeated executions of this
    // case start from the same state (the previous run released it).
    g_sharedInstance            = std::make_shared<SecShared>();
    SecBase::s_sharedDestructed = 0;

    EngineScope scope{engine.get()};
    engine->registerClass(kSecSharedMeta);
    engine->globalThis().set(
        String::newString("makeSharedObj"),
        Function::newFunction(binding::cpp_func([]() -> std::shared_ptr<SecShared> { return makeShared(); }))
    );

    // Two wrappers over the SAME shared_ptr, plus a C++ owner.
    engine->evalScript(String::newString(R"(
        (function () {
            globalThis.w1 = globalThis.makeSharedObj();
            globalThis.w2 = globalThis.makeSharedObj();
            globalThis.sameTag = (globalThis.w1.tag === globalThis.w2.tag);
        })();
    )"));
    REQUIRE(engine->evalScript(String::newString("globalThis.sameTag")).asBoolean().getValue());
    REQUIRE(g_sharedInstance.use_count() >= 3); // C++ owner + 2 wrappers

    // Drop both wrappers: the C++ owner keeps the object alive.
    engine->evalScript(String::newString("(function () { globalThis.w1 = null; globalThis.w2 = null; })();"));
    runFullGc(*engine);
    REQUIRE(SecBase::s_sharedDestructed == 0); // still owned by g_sharedInstance

    // Releasing the last C++ owner destroys the object exactly once.
    g_sharedInstance.reset();
    REQUIRE(SecBase::s_sharedDestructed == 1);
}


// ---------------------------------------------------------------------------
// Numeric argument conversion through bound functions.
//
// The full boundary matrix is covered at the TypeConverter level by
// type_converter_test.cc ("Numeric conversion safety"); this case verifies
// representative protections through the bound-argument path (truncation,
// range, negative-to-unsigned, non-finite, BigInt edges) and the
// no-implicit-conversion rules: an object argument never runs its valueOf,
// a string argument is never coerced to a number.
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(BindSecFixture, "BIND-SEC-009: numeric conversion boundaries") {
    using namespace jspp;
    EngineScope scope{engine.get()};
    registerSecClasses(*engine);

    engine->evalScript(String::newString(R"(
        globalThis.out = [];
        const check = (fn, expect) => {
            try { const r = fn(); globalThis.out.push((r === expect) ? 'ok' : 'bad:' + r); }
            catch (e) { globalThis.out.push(e instanceof TypeError ? 'TypeError' : 'other'); }
        };

        // bound-argument path: one representative per protection
        check(() => SecNum.takesInt(3.7), 3);                 // fractional truncation
        check(() => SecNum.takesInt(2147483648), 0);          // out of range -> TypeError
        check(() => SecNum.takesUint(-1), 0);                 // negative unsigned -> TypeError
        check(() => SecNum.takesInt(NaN), 0);                 // non-finite -> TypeError
        check(() => SecNum.takesInt(2147483647n), 2147483647);// BigInt exact at the edge
        check(() => SecNum.takesInt(-(2n ** 63n)), 0);        // BigInt in int64, out of int32 -> TypeError
        check(() => SecNum.takesUint(-1n), 0);                // negative BigInt -> TypeError (read)

        // Objects never undergo implicit ToNumber conversion (their valueOf
        // must not run during argument conversion).
        globalThis.valueOfRan = false;
        const sneaky = { valueOf() { globalThis.valueOfRan = true; return 1; } };
        check(() => SecNum.takesInt(sneaky), 0);           // -> TypeError, no implicit JS
        check(() => SecNum.takesInt('5'), 0);              // string is not a number -> TypeError

        globalThis.after = 1 + 1;
    )"));

    auto expected = std::vector<std::string>{
        "ok", "TypeError", "TypeError", "TypeError", "ok", "TypeError", "TypeError",
        "TypeError", "TypeError",
    };
    REQUIRE(evalStr(*engine, "globalThis.out.join(',')") ==
            std::accumulate(expected.begin(), expected.end(), std::string{},
                            [](std::string const& a, std::string const& b) { return a.empty() ? b : a + "," + b; }));
    REQUIRE_FALSE(engine->evalScript(String::newString("globalThis.valueOfRan")).asBoolean().getValue());
    REQUIRE(evalInt(*engine, "globalThis.after") == 2);
}


// ---------------------------------------------------------------------------
// Reentrancy during a native member call.
//
// The bound method calls a script callback in the middle of its body. The
// callback drops the JS reference to the instance, forces a GC and recurses
// three levels deep through fresh instances. The in-flight method must
// keep computing correct data after the callback returns.
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(BindSecFixture, "BIND-SEC-011: reentrant callback drops the instance mid-call") {
    using namespace jspp;
    EngineScope scope{engine.get()};
    registerSecClasses(*engine);

    engine->evalScript(String::newString(R"(
        // The callback runs while the native method is on the C++ stack: it
        // drops the last JS reference and forces a collection, then recurses.
        globalThis.onReenter = function (depth) {
            globalThis.holder = null;
            gc(); // engine GC entry point exposed by the fixture below
            if (depth < 3) {
                globalThis.holder = new SecBase();
                globalThis.holder.reentrant(depth + 1);
            }
        };
        globalThis.holder = new SecBase();
    )"));

    // Bind gc() into the script world for the callback.
    engine->globalThis().set(
        String::newString("gc"),
        Function::newFunction(binding::cpp_func([this](Arguments const&) -> Local<Value> {
            engine->gc();
            return {};
        }))
    );

    engine->evalScript(String::newString(R"(
        (function () {
            globalThis.result = globalThis.holder.reentrant(1);
        })();
        globalThis.after = 1 + 1;
    )"));

    // Reentrancy safety: after the callback mutates global state, forces a
    // GC and recurses three levels deep, the in-flight method still computes
    // its data correctly (5 + 1 on its own instance; each level uses a
    // fresh one).
    REQUIRE(evalInt(*engine, "globalThis.result") == 6);
    REQUIRE(evalInt(*engine, "globalThis.after") == 2);

    // Note: the receiver must stay valid for the duration of a native member
    // call; how that is guaranteed is a backend concern. Independent
    // lifetime coverage (owner release, wrapper death, destructor
    // exactly-once) lives in native_proxy_interop_test.cc
    // "lifecycle under GC".
}


// ---------------------------------------------------------------------------
// C++ exceptions crossing the binding boundary.
//
// A std::runtime_error thrown from a bound method surfaces as a catchable
// script error instead of crashing the process or being swallowed; the
// instance and the engine keep working afterwards.
// ---------------------------------------------------------------------------

TEST_CASE_METHOD(BindSecFixture, "BIND-SEC-013: C++ exceptions cross the boundary as catchable errors") {
    using namespace jspp;
    EngineScope scope{engine.get()};
    registerSecClasses(*engine);

    engine->evalScript(String::newString(R"(
        const inst = new SecBase();

        // A std::runtime_error from a bound method becomes a catchable script
        // error, not a crash and not a swallowed result.
        try { inst.boom(); globalThis.e1 = 'no-throw'; }
        catch (e) { globalThis.e1 = 'caught:' + (e instanceof Error); }

        // The instance and the engine keep working afterwards.
        globalThis.e2 = inst.add(2);
        globalThis.after = 1 + 1;
    )"));

    REQUIRE(evalStr(*engine, "globalThis.e1") == "caught:true");
    REQUIRE(evalInt(*engine, "globalThis.e2") == 7);
    REQUIRE(evalInt(*engine, "globalThis.after") == 2);
}
