#include "catch2/catch_test_macros.hpp"
#include "jspp/Jspp.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/TypeConverter.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

#include <chrono>
#ifdef _WIN32
#include <process.h>

JSPP_WARNING_GUARD_BEGIN
#include <windows.h>
JSPP_WARNING_GUARD_END
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif
#include <cstdlib>
#include <filesystem>
#include <string>
#include <thread>

#ifdef JSPP_BACKEND_V8
#include "v8-object.h"
#endif


// ===========================================================================
// Test classes: instance method / getter / setter / lifecycle counters
// ===========================================================================

namespace {

struct RxBase {
    RxBase() {
        ++s_constructed;
        ++s_alive;
    }
    virtual ~RxBase() {
        ++s_destructed;
        --s_alive;
    }

    int value = 5;

    int add(int v) {
        value += v;
        return value;
    }
    int  getValue() const { return value; }
    void setValue(int v) { value = v; }

    static int echo(int v) { return v; }

    inline static int s_alive       = 0;
    inline static int s_constructed = 0;
    inline static int s_destructed  = 0;

    static void resetCounters() {
        s_alive       = 0;
        s_constructed = 0;
        s_destructed  = 0;
    }
};

struct RxDerived : RxBase {
    RxDerived() = default;
    int sub() { return 100; }
};

struct RxOther {
    RxOther() = default;
    int other() { return 1; }
};

struct RxThrow {
    RxThrow() {
        ++s_created;
        throw std::runtime_error{"ctor-boom"};
    }
    inline static int s_created = 0;
};

int takesRxBase(RxBase const& b) { return b.value; }

auto const kRxBaseMeta = jspp::binding::defClass<RxBase>("RxBase")
                             .ctor<>()
                             .method("add", &RxBase::add)
                             .prop("value", &RxBase::getValue, &RxBase::setValue)
                             .func("echo", &RxBase::echo)
                             .build();

auto const kRxDerivedMeta =
    jspp::binding::defClass<RxDerived>("RxDerived").ctor<>().inherit<RxBase>(kRxBaseMeta).method("sub", &RxDerived::sub).build();

auto const kRxOtherMeta = jspp::binding::defClass<RxOther>("RxOther").ctor<>().method("other", &RxOther::other).build();

auto const kRxThrowMeta = jspp::binding::defClass<RxThrow>("RxThrow").ctor<>().build();

auto const kRxUtilMeta  = jspp::binding::defClass<void>("RxUtil").func("takesRxBase", &takesRxBase).build();

void registerAll(jspp::Engine& engine) {
    engine.registerClass(kRxBaseMeta);
    engine.registerClass(kRxDerivedMeta);
    engine.registerClass(kRxOtherMeta);
    engine.registerClass(kRxThrowMeta);
    engine.registerClass(kRxUtilMeta);
}

int evalInt(jspp::Engine& engine, std::string const& expr) {
    return engine.evalScript(jspp::String::newString(expr)).asNumber().getInt32();
}

std::string evalStr(jspp::Engine& engine, std::string const& expr) {
    return engine.evalScript(jspp::String::newString(expr)).asString().getValue();
}


void runFullGc(jspp::Engine& engine, int attempts = 12) {
    for (int i = 0; i < attempts; ++i) {
        engine.gc();
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
}

std::string selfExePath() {
    // Resolve the current executable independent of the working directory and
    // the build layout (single/multi-config generators, with/without the
    // .exe suffix).
#if defined(_WIN32)
    char buffer[MAX_PATH];
    if (GetModuleFileNameA(nullptr, buffer, MAX_PATH) > 0) {
        return buffer;
    }
#elif defined(__APPLE__)
    uint32_t    size = 0;
    _NSGetExecutablePath(nullptr, &size);
    std::string buffer(size, '\0');
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return buffer;
    }
#else
    char buffer[4096];
    auto len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);
    if (len > 0) {
        buffer[len] = '\0';
        return buffer;
    }
#endif
    // Fallback for platforms without the APIs above.
    for (auto const* rel : {"bin/jspp_test.exe", "../bin/jspp_test.exe", "./jspp_test.exe"}) {
        if (std::filesystem::exists(rel)) {
            return std::filesystem::absolute(rel).string();
        }
    }
    return {};
}

bool interopChildMode() {
#ifdef _WIN32
    char* buf = nullptr;
    size_t len = 0;
    if (_dupenv_s(&buf, &len, "JSPP_INTEROP_CHILD") == 0 && buf != nullptr) {
        bool enabled = (len > 1); // "1" including the terminator
        free(buf);
        return enabled;
    }
    return false;
#else
    return std::getenv("JSPP_INTEROP_CHILD") != nullptr;
#endif
}

} // namespace


struct NativeInteropFixture {
    std::unique_ptr<jspp::Engine> engine;
    NativeInteropFixture() { engine = std::make_unique<jspp::Engine>(); }
};


// ===========================================================================
// A2 / C1: normal instance paths and inheritance
// ===========================================================================

TEST_CASE_METHOD(NativeInteropFixture, "NativeInterop: normal instance paths and inheritance") {
    using namespace jspp;
    RxBase::resetCounters();
    RxThrow::s_created = 0;

    EngineScope scope{engine.get()};
    registerAll(*engine);

    engine->evalScript(String::newString(R"(
        const b = new RxBase();
        globalThis.r1 = b.add(1);                    // method: 5 + 1
        globalThis.r2 = b.value;                     // native getter
        b.value = 20;                                // native setter
        globalThis.r3 = b.value;

        const m = b.add;                             // extracted method
        globalThis.r4 = m.call(b, 5);                // original instance as receiver

        const d = new RxDerived();
        globalThis.r5 = d.add(2);                    // inherited method
        globalThis.r6 = d.value;                     // inherited getter/setter
        d.value = 9;
        globalThis.r7 = d.value;
        globalThis.r8 = d.sub();                     // derived own method

        try { RxBase.prototype.add.call(new RxOther(), 1); globalThis.r9 = 'no-throw'; }
        catch (e) { globalThis.r9 = (e instanceof TypeError) ? 'TypeError' : 'other'; }

        try { RxBase.prototype.value; globalThis.r10 = 'no-throw'; }
        catch (e) { globalThis.r10 = (e instanceof TypeError) ? 'TypeError' : 'other'; }

        globalThis.r11 = RxUtil.takesRxBase(b);      // toCpp<RxBase> succeeds
        globalThis.r12 = RxBase.echo(5);             // static member
    )"));

    REQUIRE(evalInt(*engine, "globalThis.r1") == 6);
    REQUIRE(evalInt(*engine, "globalThis.r2") == 6);
    REQUIRE(evalInt(*engine, "globalThis.r3") == 20);
    REQUIRE(evalInt(*engine, "globalThis.r4") == 25); // 20 + 5
    REQUIRE(evalInt(*engine, "globalThis.r5") == 7);
    REQUIRE(evalInt(*engine, "globalThis.r6") == 7);
    REQUIRE(evalInt(*engine, "globalThis.r7") == 9);
    REQUIRE(evalInt(*engine, "globalThis.r8") == 100);
    REQUIRE(evalStr(*engine, "globalThis.r9") == "TypeError"); // unrelated native class
    REQUIRE(evalStr(*engine, "globalThis.r10") == "TypeError"); // accessor without instance receiver
    REQUIRE(evalInt(*engine, "globalThis.r11") == 25);
    REQUIRE(evalInt(*engine, "globalThis.r12") == 5);

    // C++-side construction through Engine::newInstance()
    auto cppObj = engine->newInstance(kRxBaseMeta, jspp::binding::factory::newNativeInstance<RxBase>());
    REQUIRE(cppObj.get(String::newString("value")).asNumber().getInt32() == 5);

    // Prototype-chain forgery: instanceof passes, but no payload exists.
    engine->evalScript(String::newString(R"(
        const fake = {};
        Object.setPrototypeOf(fake, RxBase.prototype);
        globalThis.fakeInstanceof = (fake instanceof RxBase);
        try { fake.add(1); globalThis.fakeCall = 'no-throw'; }
        catch (e) { globalThis.fakeCall = (e instanceof TypeError) ? 'TypeError' : 'other'; }
    )"));
    REQUIRE(engine->evalScript(String::newString("globalThis.fakeInstanceof")).asBoolean().getValue());
    REQUIRE(evalStr(*engine, "globalThis.fakeCall") == "TypeError");

    REQUIRE(RxBase::s_alive >= 1); // b, d, cppObj alive until scope end
}


// ===========================================================================
// A3: invalid receiver matrix (method / getter / setter / extracted method)
// ===========================================================================

TEST_CASE_METHOD(NativeInteropFixture, "NativeInterop: invalid receivers are rejected with TypeError") {
    using namespace jspp;
    RxBase::resetCounters();

    EngineScope scope{engine.get()};
    registerAll(*engine);

    engine->evalScript(String::newString(R"(
        const inst = new RxBase();
        inst.value = 7;
        const proto = Object.getPrototypeOf(inst);
        const desc  = Object.getOwnPropertyDescriptor(proto, 'value');
        const other = new RxOther();

        const receivers = [
            undefined, null, 42, 'str', true,
            {},                          // plain object
            Object.create(inst),         // child of the instance
            Object.create(proto),        // clone of the prototype
            other,                       // unrelated native class
            new Proxy(inst, {}),         // transparent proxy
            new Proxy(inst, { get(t, k) { return Reflect.get(t, k, inst); } }), // custom trap
            (p => { p.revoke(); return p.proxy; })(Proxy.revocable(inst, {}))  // revoked
        ];

        globalThis.results = [];
        receivers.forEach((R) => {
            const outcomes = [];
            const check = (fn) => {
                try { void fn(); outcomes.push('no-throw'); }
                catch (e) { outcomes.push(e instanceof TypeError ? 'TypeError' : 'other'); }
            };
            check(() => proto.add.call(R, 1));            // method
            check(() => Reflect.apply(proto.add, R, [1])); // extracted + Reflect.apply
            check(() => desc.get.call(R));                 // getter via descriptor
            check(() => desc.set.call(R, 99));             // setter via descriptor
            globalThis.results.push(outcomes.join(','));
        });

        globalThis.instanceIntact = (inst.value === 7);
        globalThis.after = 1 + 1; // the engine keeps working after the storm
    )"));

    auto results = engine->evalScript(String::newString("globalThis.results.join('|')")).asString().getValue();
    auto parts   = std::vector<std::string>{};
    size_t start = 0;
    while (true) {
        auto pos = results.find('|', start);
        parts.push_back(results.substr(start, pos - start));
        if (pos == std::string::npos) break;
        start = pos + 1;
    }
    REQUIRE(parts.size() == 12);
    for (auto const& p : parts) {
        CAPTURE(p);
        REQUIRE(p == "TypeError,TypeError,TypeError,TypeError");
    }

    // The member callbacks never ran: state untouched.
    REQUIRE(engine->evalScript(String::newString("globalThis.instanceIntact")).asBoolean().getValue());
    REQUIRE(evalInt(*engine, "globalThis.after") == 2);
}


// ===========================================================================
// A receiver built by a foreign engine template with the same internal field
// count as jspp classes.
//
// Before the receiver checks were added, reading such an object's fields
// could abort the process outright (V8 terminates on out-of-bounds internal
// field access), which the test framework cannot catch. This case therefore
// only performs its work when spawned as a dedicated child process: a
// regression shows up as the wrapper case below failing, while the rest of
// the suite still reports normally.
//
// The foreign receiver is driven through the method, the descriptor getter
// and the descriptor setter, through the native-instance conversion, and
// through trySetReferenceInternal; every path must reject it safely without
// reading or writing the stored foreign pointer.
// ===========================================================================

TEST_CASE_METHOD(NativeInteropFixture, "NativeInterop: foreign receiver trio") {
    using namespace jspp;

    if (!interopChildMode()) {
        SUCCEED("runs only in the dedicated child process");
        return;
    }

    EngineScope scope{engine.get()};
    registerAll(*engine);

    auto makeForeign = [&]() -> jspp::Local<Object> {
#ifdef JSPP_BACKEND_V8
        // An object from a foreign ObjectTemplate with the same internal field
        // count, holding an unrelated (but valid, aligned) pointer.
        static int dummyStorage = 0;
        auto*     isolate       = engine->isolate();
        auto      tpl           = v8::ObjectTemplate::New(isolate);
        tpl->SetInternalFieldCount(2);
        auto maybe = tpl->NewInstance(engine->context());
        if (maybe.IsEmpty()) {
            throw Exception{"failed to create the foreign template object"};
        }
        maybe.ToLocalChecked()->SetAlignedPointerInInternalField(0, static_cast<void*>(&dummyStorage));
        return ValueHelper::wrap<Object>(maybe.ToLocalChecked());
#else
        // QuickJS equivalent: a jspp native function object; its class stores an
        // opaque pointer (the callback) that is not an InstancePayload.
        auto fn = Function::newFunction([](Arguments const&) -> Local<Value> { return {}; });
        return fn.asValue().asObject();
#endif
    };
    auto foreign = makeForeign();

    engine->globalThis().set(String::newString("foreign"), foreign);
    engine->evalScript(String::newString(R"(
        const proto = RxBase.prototype;
        const desc  = Object.getOwnPropertyDescriptor(proto, 'value');
        globalThis.o = [];
        const check = (fn) => {
            try { void fn(); globalThis.o.push('no-throw'); }
            catch (e) { globalThis.o.push(e instanceof TypeError ? 'TypeError' : 'other'); }
        };
        check(() => proto.add.call(globalThis.foreign, 1));
        check(() => desc.get.call(globalThis.foreign));
        check(() => desc.set.call(globalThis.foreign, 1));
        check(() => RxUtil.takesRxBase(globalThis.foreign)); // toCpp-style conversion
    )"));

    auto outcomes = evalStr(*engine, "globalThis.o.join(',')");
    REQUIRE(outcomes == "TypeError,TypeError,TypeError,TypeError");

    // trySetReferenceInternal refuses foreign objects without touching them.
    auto inst = engine->evalScript(String::newString("new RxBase()")).asObject();
    REQUIRE_FALSE(engine->trySetReferenceInternal(foreign, inst));
    REQUIRE_FALSE(engine->trySetReferenceInternal(inst, foreign));
    REQUIRE(engine->trySetReferenceInternal(inst, inst));
}

TEST_CASE_METHOD(NativeInteropFixture, "NativeInterop: fatal-prone receiver cases run in a child process") {
    // Runs "foreign receiver trio" in a dedicated child process (marked with
    // JSPP_INTEROP_CHILD): a regression there can abort the process, which
    // must not take down the whole suite.
    auto exe = selfExePath();
    REQUIRE_FALSE(exe.empty());

#ifdef _WIN32
    _putenv("JSPP_INTEROP_CHILD=1");
    // _spawnl does not quote arguments itself: quote the filter so the child
    // receives it as a single argv entry.
    auto code = _spawnl(_P_WAIT, exe.c_str(), exe.c_str(), "\"NativeInterop: foreign receiver trio\"", nullptr);
    _putenv("JSPP_INTEROP_CHILD=");
#else
    setenv("JSPP_INTEROP_CHILD", "1", 1);
    auto command = "\"" + exe + "\" \"NativeInterop: foreign receiver trio\"";
    auto code    = std::system(command.c_str());
    unsetenv("JSPP_INTEROP_CHILD");
#endif
    REQUIRE(code == 0);
}


// ===========================================================================
// B: Proxy x NativeInstance interop
// ===========================================================================

TEST_CASE_METHOD(NativeInteropFixture, "NativeInterop: transparent proxy over a native instance") {
    using namespace jspp;
    RxBase::resetCounters();

    EngineScope scope{engine.get()};
    registerAll(*engine);

    engine->evalScript(String::newString(R"(
        const inst = new RxBase();
        inst.extra = 'data'; // plain dynamic data property on the wrapper
        const px = new Proxy(inst, {});

        globalThis.d1 = (px.extra === 'data');
        px.extra = 'data2';
        globalThis.d2 = (inst.extra === 'data2');

        const check = (fn) => {
            try { void fn(); return 'no-throw'; }
            catch (e) { return e instanceof TypeError ? 'TypeError' : 'other'; }
        };
        // jspp-bound members depend on the native instance receiver: a proxy
        // receiver does not carry the payload (internal slots do not propagate).
        globalThis.d3 = check(() => px.add(1));
        globalThis.d4 = check(() => px.value);
        globalThis.d5 = check(() => { px.value = 20; });

        globalThis.instanceIntact = (inst.value === 5);
        globalThis.after = 1 + 1;
    )"));

    REQUIRE(engine->evalScript(String::newString("globalThis.d1")).asBoolean().getValue());
    REQUIRE(engine->evalScript(String::newString("globalThis.d2")).asBoolean().getValue());
    REQUIRE(evalStr(*engine, "globalThis.d3") == "TypeError");
    REQUIRE(evalStr(*engine, "globalThis.d4") == "TypeError");
    REQUIRE(evalStr(*engine, "globalThis.d5") == "TypeError");
    REQUIRE(engine->evalScript(String::newString("globalThis.instanceIntact")).asBoolean().getValue());
    REQUIRE(evalInt(*engine, "globalThis.after") == 2);
}

TEST_CASE_METHOD(NativeInteropFixture, "NativeInterop: adapted handler forwards native members") {
    using namespace jspp;
    RxBase::resetCounters();

    EngineScope scope{engine.get()};
    registerAll(*engine);

    engine->evalScript(String::newString(R"(
        const inst = new RxBase();
        const px = new Proxy(inst, {
            get(target, key) {
                const value = Reflect.get(target, key, target); // receiver = target
                return (key !== 'value' && typeof value === 'function')
                    ? value.bind(target)
                    : value;
            },
            set(target, key, value) { return Reflect.set(target, key, value, target); }
        });

        globalThis.a1 = px.add(1);      // bound native method on the target
        globalThis.a2 = px.value;       // native getter with target receiver
        px.value = 30;                  // native setter with target receiver
        globalThis.a3 = inst.value;     // the native state actually changed
        globalThis.a4 = RxUtil.takesRxBase(inst); // toCpp still fine on the target
    )"));

    REQUIRE(evalInt(*engine, "globalThis.a1") == 6);
    REQUIRE(evalInt(*engine, "globalThis.a2") == 6);
    REQUIRE(evalInt(*engine, "globalThis.a3") == 30);
    REQUIRE(evalInt(*engine, "globalThis.a4") == 30);
    REQUIRE(RxBase::s_constructed == 1); // no duplicated instances
}

TEST_CASE_METHOD(NativeInteropFixture, "NativeInterop: proxy conversions and instanceof") {
    using namespace jspp;
    RxBase::resetCounters();

    EngineScope scope{engine.get()};
    registerAll(*engine);

    engine->evalScript(String::newString(R"(
        const inst = new RxBase();
        globalThis.px = new Proxy(inst, {});
        globalThis.jsInstanceof = (globalThis.px instanceof RxBase);
    )"));

    // Script instanceof walks the proxy prototype trap: true on both engines.
    REQUIRE(engine->evalScript(String::newString("globalThis.jsInstanceof")).asBoolean().getValue());

    // Engine::isInstanceOf implements the same ECMA semantics on every backend
    // (QuickJS: JS_IsInstanceOf; V8: Value::InstanceOf): proxy traps and
    // Symbol.hasInstance are honored, so a proxied instance reports true.
    // instanceof success is still not evidence of a usable payload.
    REQUIRE(engine->isInstanceOf(engine->evalScript(String::newString("globalThis.px")).asObject(), kRxBaseMeta));

    // instanceof success is never evidence of a usable payload: conversion and
    // member calls still fail safely.
    engine->evalScript(String::newString(R"(
        try { RxUtil.takesRxBase(globalThis.px); globalThis.c1 = 'no-throw'; }
        catch (e) { globalThis.c1 = (e instanceof TypeError) ? 'TypeError' : 'other'; }
        try { globalThis.px.add(1); globalThis.c2 = 'no-throw'; }
        catch (e) { globalThis.c2 = (e instanceof TypeError) ? 'TypeError' : 'other'; }
        globalThis.after = 1 + 1; // failed conversions do not pollute later use
    )"));
    REQUIRE(evalStr(*engine, "globalThis.c1") == "TypeError");
    REQUIRE(evalStr(*engine, "globalThis.c2") == "TypeError");
    REQUIRE(evalInt(*engine, "globalThis.after") == 2);
}


// ===========================================================================
// C2: constructor paths
// ===========================================================================

TEST_CASE_METHOD(NativeInteropFixture, "NativeInterop: constructor paths and failure safety") {
    using namespace jspp;
    RxBase::resetCounters();
    RxThrow::s_created = 0;

    EngineScope scope{engine.get()};
    registerAll(*engine);

    // A proxied constructor forwards [[Construct]] and yields a real instance.
    engine->evalScript(String::newString(R"(
        const Wrapped = new Proxy(RxBase, {});
        const i = new Wrapped();
        globalThis.w1 = i.add(1);
        globalThis.w2 = i.value;
        i.value = 8;
        globalThis.w3 = i.value;

        // A throwing C++ constructor: the pending instance is destroyed (RAII),
        // the exception is catchable, and the engine keeps working.
        try { new RxThrow(); globalThis.t1 = 'no-throw'; }
        catch (e) { globalThis.t1 = 'caught'; }
        globalThis.after = 1 + 1;
    )"));
    REQUIRE(evalInt(*engine, "globalThis.w1") == 6);
    REQUIRE(evalInt(*engine, "globalThis.w2") == 6);
    REQUIRE(evalInt(*engine, "globalThis.w3") == 8);
    REQUIRE(evalStr(*engine, "globalThis.t1") == "caught");
    REQUIRE(RxThrow::s_created == 1); // the native constructor ran exactly once
    REQUIRE(evalInt(*engine, "globalThis.after") == 2);

    // C++ side construction with a throwing... (newInstance adopts the
    // instance through the transfer holder; failure would leak, ASAN checks)
    auto before = RxBase::s_constructed;
    {
        auto obj = engine->newInstance(kRxBaseMeta, jspp::binding::factory::newNativeInstance<RxBase>());
        REQUIRE(obj.get(String::newString("value")).asNumber().getInt32() == 5);
    }
    REQUIRE(RxBase::s_constructed == before + 1);
}


// ===========================================================================
// D: GC and lifecycle
// ===========================================================================

TEST_CASE_METHOD(NativeInteropFixture, "NativeInterop: lifecycle under GC") {
    using namespace jspp;
    RxBase::resetCounters();

    EngineScope scope{engine.get()};
    registerAll(*engine);

    // C++-side construction entry point. Scripts below call it from inside an
    // IIFE: on V8 the completion value of a script is stored as a handle in
    // the current HandleScope, and dropping the returned Local does not clear
    // that slot (see the note on Engine::evalScript). A script like
    // `globalThis.holder = new RxBase();` would keep the instance reachable
    // from the enclosing EngineScope, so `holder = null` could never make it
    // collectable. An IIFE leaves `undefined` as the completion value.
    // QuickJS is unaffected (explicitly refcounted JSValue).
    engine->globalThis().set(
        String::newString("makeRxBase"),
        Function::newFunction(binding::cpp_func([]() -> Local<Object> {
            auto& engine = EngineScope::currentEngineChecked();
            return engine.newInstance(kRxBaseMeta, binding::factory::newNativeInstance<RxBase>());
        }))
    );

    // Reachable instances survive GC.
    engine->evalScript(String::newString("(function () { globalThis.holder = new RxBase(); })();"));
    runFullGc(*engine, 4);
    REQUIRE(RxBase::s_alive == 1);

    // Losing the reference releases the instance exactly once.
    engine->evalScript(String::newString("(function () { globalThis.holder = null; })();"));
    runFullGc(*engine);
    REQUIRE(RxBase::s_alive == 0);
    REQUIRE(RxBase::s_destructed == 1);

    // Repeated GC does not double free.
    runFullGc(*engine, 4);
    REQUIRE(RxBase::s_destructed == 1);

    // A proxy keeps its target alive; dropping the proxy releases it once.
    RxBase::resetCounters();
    engine->evalScript(String::newString("(function () { globalThis.holder = new Proxy(new RxBase(), {}); })();"));
    runFullGc(*engine, 4);
    REQUIRE(RxBase::s_alive == 1);
    engine->evalScript(String::newString("(function () { globalThis.holder = null; })();"));
    runFullGc(*engine);
    REQUIRE(RxBase::s_alive == 0);
    REQUIRE(RxBase::s_destructed == 1);

    // A C++-constructed instance is released as well once unreachable.
    RxBase::resetCounters();
    engine->evalScript(String::newString("(function () { globalThis.cppInst = globalThis.makeRxBase(); })();"));
    runFullGc(*engine, 4);
    REQUIRE(RxBase::s_alive == 1);
    engine->evalScript(String::newString("(function () { globalThis.cppInst = null; })();"));
    runFullGc(*engine);
    REQUIRE(RxBase::s_destructed == 1);

    // An extracted method outliving its instance: a later call with an invalid
    // receiver must be rejected without touching freed memory.
    RxBase::resetCounters();
    engine->evalScript(String::newString(
        "(function () { globalThis.extracted = (new RxBase()).add; })(); true;"
    ));
    runFullGc(*engine);
    REQUIRE(RxBase::s_alive == 0);
    engine->evalScript(String::newString(R"(
        try { globalThis.extracted.call({}, 1); globalThis.x1 = 'no-throw'; }
        catch (e) { globalThis.x1 = (e instanceof TypeError) ? 'TypeError' : 'other'; }
        globalThis.after = 1 + 1;
    )"));
    REQUIRE(evalStr(*engine, "globalThis.x1") == "TypeError");
    REQUIRE(evalInt(*engine, "globalThis.after") == 2);
}


// ===========================================================================
// E: proxy traps and native callbacks
// ===========================================================================

TEST_CASE_METHOD(NativeInteropFixture, "NativeInterop: proxy traps and native callbacks") {
    using namespace jspp;
    EngineScope scope{engine.get()};
    registerAll(*engine);

    int  getCalls     = 0;
    int  deleteCalls  = 0;
    int  applyCalls   = 0;
    bool deleteResult = false;

    auto handler = Object::newObject();
    handler.set(
        String::newString("get"),
        Function::newFunction([&](Arguments const& a) -> Local<Value> {
            ++getCalls;
            return String::newString("g");
        })
    );
    handler.set(
        String::newString("deleteProperty"),
        Function::newFunction([&](Arguments const& a) -> Local<Value> {
            ++deleteCalls;
            return Boolean::newBoolean(deleteResult = true);
        })
    );

    auto target = Object::newObject();
    target.set(String::newString("k"), Number::newNumber(1));
    engine->globalThis().set(String::newString("px"), Proxy::newProxy(target, handler));

    // Script side drives native traps synchronously (get / deleteProperty).
    engine->evalScript(String::newString(R"(
        globalThis.g1 = globalThis.px.k;
        globalThis.g2 = delete globalThis.px.k;
    )"));
    REQUIRE(getCalls == 1);
    REQUIRE(deleteCalls == 1);
    REQUIRE(deleteResult);
    REQUIRE(evalStr(*engine, "globalThis.g1") == "g");
    REQUIRE(engine->evalScript(String::newString("globalThis.g2")).asBoolean().getValue());

    // A native function behind an apply trap: the trap intercepts the call.
    auto nativeFn = Function::newFunction([](Arguments const& a) -> Local<Value> {
        return Number::newNumber(-1); // must be intercepted, never returned
    });
    engine->globalThis().set(String::newString("nativeFn"), nativeFn);
    engine->evalScript(String::newString(R"(
        globalThis.pf = new Proxy(globalThis.nativeFn, {
            apply(target, thisArg, args) {
                globalThis.applyArg = args[0];
                return 'intercepted';
            }
        });
        globalThis.applied = globalThis.pf(21);
    )"));
    REQUIRE(evalInt(*engine, "globalThis.applyArg") == 21);
    REQUIRE(evalStr(*engine, "globalThis.applied") == "intercepted");

    // Static members do not consult an instance payload.
    engine->evalScript(String::newString(R"(
        globalThis.s1 = RxBase.echo(5);
        globalThis.s2 = (new Proxy(RxBase, {})).echo(6);
    )"));
    REQUIRE(evalInt(*engine, "globalThis.s1") == 5);
    REQUIRE(evalInt(*engine, "globalThis.s2") == 6);
}
