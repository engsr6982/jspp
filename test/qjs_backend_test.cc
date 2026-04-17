#ifdef JSPP_BACKEND_QUICKJS
#include "jspp/Jspp.h"
#include "jspp/binding/TypeConverter.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Trampoline.h"
#include "jspp/core/Value.h"

#include "jspp-backend/queue/JobQueue.h"

#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/MetaBuilder.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_exception.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

#include <iostream>


struct QjsTestFixture {
    std::unique_ptr<jspp::Engine> engine;
    explicit QjsTestFixture() : engine(std::make_unique<jspp::Engine>()) {}
};

static int g_parent_deleted = 0;
static int g_child_deleted  = 0;

class GcChild {
public:
    GcChild() = default;
    ~GcChild() { g_child_deleted++; }
    void ping() {}
};

class GcParent : public jspp::enable_trampoline {
    GcChild child_;

public:
    GcParent() = default;
    virtual ~GcParent() { g_parent_deleted++; }
    GcChild&     getChild() { return child_; }
    virtual void virtualFunc() {}
};

auto GcChildMeta = jspp::binding::defClass<GcChild>("GcChild").ctor<>().method("ping", &GcChild::ping).build();

auto GcParentMeta = jspp::binding::defClass<GcParent>("GcParent")
                        .ctor<>()
                        .method("getChild", &GcParent::getChild, jspp::binding::ReturnValuePolicy::kReferenceInternal)
                        .method("virtualFunc", &GcParent::virtualFunc)
                        .build();

TEST_CASE_METHOD(QjsTestFixture, "GC Behavior - Trampoline Cycle Collection") {
    jspp::EngineScope scope{engine.get()};
    engine->registerClass(GcParentMeta);
    g_parent_deleted = 0;

    engine->evalScript(jspp::String::newString(R"(
        class MyParent extends GcParent {
            virtualFunc() { } // Trigger override mount
        }
        function createCycle() {
            let obj = new MyParent();
            // At this time, obj is a JS object, internally mounted with a C GcParent instance
            // GcParent inherits from enable_trampoline and internally holds a reference pointing back to the JS object.
            // Formed a classic: JS Obj -> C Obj -> JS Obj strong circular reference!
        }
        createCycle();
    )"));

    // Trigger forced garbage collection
    // At this point, QuickJS will detect cyclic references through NativeClassGcMarker and safely break them.
    engine->gc();

    REQUIRE(g_parent_deleted == 1);
}

TEST_CASE_METHOD(QjsTestFixture, "GC Behavior - ReturnValuePolicy::kReferenceInternal") {
    jspp::EngineScope scope{engine.get()};
    engine->registerClass(GcChildMeta);
    engine->registerClass(GcParentMeta);
    g_parent_deleted = 0;
    g_child_deleted  = 0;

    engine->evalScript(jspp::String::newString(R"(
        globalThis.p = new GcParent();
        globalThis.c = p.getChild();  // Trigger kReferenceInternal
        globalThis.p = null;          // Discard the JS reference of the parent object
    )"));

    engine->gc();

    // Because the child object `c` is still being used in JS, the parent object absolutely cannot be collected!
    REQUIRE(g_parent_deleted == 0);

    engine->evalScript(jspp::String::newString(R"(
        globalThis.c.ping(); // Confirm that it can still be called safely
        globalThis.c = null; // Completely discard child objects
    )"));

    engine->gc();

    // The child object reference is cleared, the internal hidden properties are released accordingly,
    //  and the parent object is safely collected!
    REQUIRE(g_parent_deleted == 1);
}


class Base {
public:
    inline static constexpr int static_property = 1;

    static int getStaticProperty() { return static_property; }

    Base() = default;

    std::string base_member = "1234";

    std::string const& getMember() { return base_member; }
};
class Derived : public Base {
public:
    inline static constexpr std::string_view der_constant = "derived";

    static std::string_view getDerConstant() { return der_constant; }

    Derived() = default;

    std::string der_member = "der_mem";

    std::string getDerMem() { return der_member; }
};
auto BaseMeta = jspp::binding::defClass<Base>("Base")
                    .var("static_property", &Base::static_property)
                    .func("getStaticProperty", &Base::getStaticProperty)
                    .ctor()
                    .prop("base_member", &Base::base_member)
                    .method("getMember", &Base::getMember)
                    .build();
auto DerivedMeta = jspp::binding::defClass<Derived>("Derived")
                       .var("der_constant", &Derived::der_constant)
                       .func("getDerConstant", &Derived::getDerConstant)
                       .ctor()
                       .inherit<Base>(BaseMeta)
                       .prop("der_member", &Derived::der_member)
                       .method("getDerMem", &Derived::getDerMem)
                       .build();

TEST_CASE_METHOD(QjsTestFixture, "Test Qjs class inherit") {
    using namespace jspp;
    EngineScope lock{engine.get()};

    engine->registerClass(BaseMeta);
    engine->registerClass(DerivedMeta);

    // ensure class register
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        if (Base == null) {
            throw new Error("Base not register")
        }
        if (Derived == null) {
            throw new Error("Derived not regidter")
        }
    )")));

    // ensure class access static getter
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        if (Base.static_property == null) throw "static_property not register";
        if (Base.getStaticProperty == null) throw "getStaticProperty not register";

        if (Base.static_property != 1)
            throw `static_property value mismatch, expected: ${Base.static_property}`;
        if (Base.getStaticProperty() != Base.static_property)
            throw `Base.getStaticProperty() return value mismatch!`;

        if (Derived.der_constant == null) throw 'Derived.der_constant not register';
        if (Derived.getDerConstant == null) throw 'Derived.getDerConstant not register';
        
        if (Derived.der_constant != 'derived')
            throw `der_constant value mismatch, expected: ${Derived.der_constant}`;
        if (Derived.getDerConstant() != Derived.der_constant)
            throw `Derived.getDerConstant() return value mismatch!`;
    )")));

    // ensure access static property from derived
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        if (Derived.static_property == null) {
            throw 'cannot access static_property from Derived class'
        }
        if (Derived.static_property != 1) {
            throw `der_constant value mismatch, expected: ${Derived.der_constant}`
        }
    )")));

    // ensure instnace class
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        let base1 = new Base();
        if (base1.base_member != '1234') throw 'expected Base.base_member'
        if (base1.base_member != base1.getMember()) throw 'expected Base.getMember()'

        let derived = new Derived();
        if (derived.der_member != 'der_mem') throw 'expected Derived.der_mem';
        if (derived.getDerMem() != 'der_mem') throw 'expected Derived.getDerMem()';

        if (derived.base_member != '1234') throw 'expected Derived.base_member'
        if (derived.getMember() != '1234') throw 'expected Derived.getMember()'
    )")));
}


TEST_CASE_METHOD(QjsTestFixture, "JobQueue with promise") {
    using namespace jspp;
    EngineScope lock{engine.get()};

    bool done = false;
    auto test = Function::newFunction([&done](Arguments const& args) -> Local<Value> {
        REQUIRE(args.length() == 1);
        REQUIRE(args[0].isBoolean());
        done = args[0].asBoolean().getValue();
        return {};
    });

    engine->globalThis().set(String::newString("setDone"), test);

    engine->evalScript(String::newString(R"(
        new Promise((resolve, reject) => {
            resolve();
        }).then(() => {
            new Promise((resolve, reject) => {
                resolve();
            }).then(() => {
                setDone(true);
            });
        });
    )"));
    engine->getJobQueue()->shutdown(true);
    engine->getJobQueue()->loopAndWait();
    REQUIRE(done == true);
}

#endif