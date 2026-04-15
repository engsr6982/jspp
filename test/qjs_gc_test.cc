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


#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/MetaBuilder.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_exception.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

#include <iostream>


struct QjsGCTestFixture {
    std::unique_ptr<jspp::Engine> engine;
    explicit QjsGCTestFixture() : engine(std::make_unique<jspp::Engine>()) {}
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

TEST_CASE_METHOD(QjsGCTestFixture, "GC Behavior - Trampoline Cycle Collection") {
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

TEST_CASE_METHOD(QjsGCTestFixture, "GC Behavior - ReturnValuePolicy::kReferenceInternal") {
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


#endif