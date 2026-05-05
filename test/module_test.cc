#include "catch2/catch_approx.hpp"
#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_exception.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

#include "jspp/Jspp.h"
#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/binding/TypeConverter.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Trampoline.h"
#include "jspp/core/Value.h"

#ifdef JSPP_BACKEND_QUICKJS
#include "jspp-backend/queue/JobQueue.h"
#endif

#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>

namespace ut {

using namespace jspp;
using namespace jspp::binding;

// ============================================================================
// C++ virtual business class and enum for testing
// ============================================================================

class ModuleActor {
public:
    int id        = 0;
    ModuleActor() = default;
    ModuleActor(int id) : id(id) {}
    int getId() const { return id; }
};

static auto ModuleActorMeta = defClass<ModuleActor>("Actor").ctor<int>().method("getId", &ModuleActor::getId).build();

enum class ModuleState { Idle = 0, Running = 1 };

static auto ModuleStateMeta =
    defEnum<ModuleState>("State").value("Idle", ModuleState::Idle).value("Running", ModuleState::Running).build();

int mathOp1(int a) { return a * a; }
int mathOp2(int a, int b) { return a + b; }

// ============================================================================

static auto SysModuleMeta =
    defModule("sys")
        .exportConstant("VERSION", "1.0.0")
        .exportFunction("ping", []() { return "pong"; })
        .exportFunction("mathOp", static_cast<int (*)(int)>(&mathOp1), static_cast<int (*)(int, int)>(&mathOp2))
        .exportClass(ModuleActorMeta)
        .exportEnum(ModuleStateMeta)
        .build();

struct ModState {
    int         invokeCount = 0;
    std::string lastLog; // For static class/namespace testing
};

static int counterFunc() {
    auto engine = EngineScope::currentEngine();
    auto state  = engine->getData<ModState>();
    return ++state->invokeCount;
}

static auto ModConstMeta   = defModule("mod_const").exportDefaultAsConstant(&counterFunc).build();
static auto ModFuncMeta    = defModule("mod_func").exportDefaultAsFunc(&counterFunc).build();
static auto ModLiteralMeta = defModule("mod_literal").exportDefaultAsConstant(42).build();
static auto ModClassMeta   = defModule("mod_class").exportDefault(ModuleActorMeta).build();
static auto ModEnumMeta    = defModule("mod_enum").exportDefault(ModuleStateMeta).build();

static auto NsClassMeta =
    defClass<ModuleActor>("core.net.Connection").ctor<int>().method("getId", &ModuleActor::getId).build();
static auto NsEnumMeta = defEnum<ModuleState>("core.net.Status").value("Idle", ModuleState::Idle).build();

static auto NetModuleMeta = defModule("net").exportClass(NsClassMeta).exportEnum(NsEnumMeta).build();

static auto SharedMathMeta = defModule("shared_math").exportConstant("PI", 3.14159).build();

// 静态类以及命名空间静态类 Meta 声明
static auto MathUtilsMeta =
    defClass<void>("MathUtils").func("add", [](int a, int b) { return a + b; }).var_readonly("PI", 3.14159).build();

static void Logger_log(std::string msg) {
    auto engine    = EngineScope::currentEngine();
    auto state     = engine->getData<ModState>();
    state->lastLog = std::move(msg);
}

static auto NsLoggerMeta = defClass<void>("core.utils.Logger").func("log", &Logger_log).build();

static auto StaticClassModuleMeta =
    defModule("static_mod").exportClass(MathUtilsMeta).exportClass(NsLoggerMeta).build();


// ============================================================================
// Isolated and secure testing auxiliary context and filesystem tools
// ============================================================================

struct ScopedTempDir {
    std::filesystem::path dir;

    ScopedTempDir(std::string_view name) {
        dir = std::filesystem::temp_directory_path() / name;
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir);
    }

    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    void writeFile(std::string_view filename, std::string_view content) {
        auto path = dir / filename;
        if (path.has_parent_path()) {
            auto parent = path.parent_path();
            if (!std::filesystem::exists(parent)) {
                std::filesystem::create_directories(parent);
            }
        }
        std::ofstream out(path, std::ios::binary);
        out << content;
    }

    std::string getFileUrl(std::string_view filename) {
        auto path = (dir / filename).generic_string();
        if (!path.empty() && path[0] != '/') {
            return "file:///" + path;
        }
        return "file://" + path;
    }
};

static std::unique_ptr<Engine> createModuleEngine() {
    auto engine = std::make_unique<Engine>();
    engine->setData(std::make_shared<ModState>());
    return engine;
}

// ============================================================================
// Unit Test
// ============================================================================

TEST_CASE("Module: Basic Exports (Class, Enum, Function, Constant)", "[module]") {
    auto        engine = createModuleEngine();
    EngineScope scope{engine.get()};
    engine->registerModule(SysModuleMeta);

    REQUIRE_NOTHROW(engine->evalModule(String::newString(R"(
        import * as sys from 'sys';
        globalThis.sys = sys;
    )")));

    auto sys = engine->globalThis().get(String::newString("sys")).asObject();

    // Constant export
    REQUIRE(sys.get(String::newString("VERSION")).asString().getValue() == "1.0.0");

    // Function export
    auto pingFunc = sys.get(String::newString("ping")).asFunction();
    REQUIRE(pingFunc.call(sys, {}).asString().getValue() == "pong");

    // Overloaded function
    auto mathOpFunc = sys.get(String::newString("mathOp")).asFunction();
    REQUIRE(mathOpFunc.call(sys, {Number::newNumber(5)}).asNumber().getInt32() == 25);
    REQUIRE(mathOpFunc.call(sys, {Number::newNumber(5), Number::newNumber(5)}).asNumber().getInt32() == 10);

    // Class constructor & method export
    auto ActorClass = sys.get(String::newString("Actor")).asFunction();
    auto actorObj   = ActorClass.callAsConstructor({Number::newNumber(10)}).asObject();
    auto getIdFunc  = actorObj.get(String::newString("getId")).asFunction();
    REQUIRE(getIdFunc.call(actorObj, {}).asNumber().getInt32() == 10);

    // Enum export
    auto StateEnum = sys.get(String::newString("State")).asObject();
    REQUIRE(StateEnum.get(String::newString("Running")).asNumber().getInt32() == 1);
    REQUIRE(StateEnum.get(String::newString("$name")).asString().getValue() == "State");
}

TEST_CASE("Module: Static Imports of Native Modules (ESM Syntax)", "[module]") {
    auto        engine = createModuleEngine();
    EngineScope scope{engine.get()};
    engine->registerModule(SysModuleMeta);
    engine->registerModule(SharedMathMeta);
    engine->registerModule(ModFuncMeta);

    REQUIRE_NOTHROW(engine->evalModule(String::newString(R"(
        import * as sys_ns from 'sys';
        import { Actor, State } from 'sys';
        import { PI } from 'shared_math';
        import counter from 'mod_func';

        globalThis.static_test_res = {
            ver: sys_ns.VERSION,
            actorId: new Actor(100).getId(),
            state: State.Running,
            pi: PI,
            count: counter()
        };
    )")));

    auto res = engine->globalThis().get(String::newString("static_test_res")).asObject();
    REQUIRE(res.get(String::newString("ver")).asString().getValue() == "1.0.0");
    REQUIRE(res.get(String::newString("actorId")).asNumber().getInt32() == 100);
    REQUIRE(res.get(String::newString("state")).asNumber().getInt32() == 1);
    REQUIRE(res.get(String::newString("pi")).asNumber().getValueAs<double>() == Catch::Approx(3.14159));
    REQUIRE(res.get(String::newString("count")).asNumber().getInt32() == 1);
}

TEST_CASE("Module: Built-in Loader System (Relative File Imports)", "[module]") {
    auto          engine = createModuleEngine();
    ScopedTempDir dir("jspp_loader_system_test");

    dir.writeFile("a.js", R"(
        export const VAL_A = 100;
        export function getA() { return VAL_A; }
    )");

    dir.writeFile("b.js", R"(
        import { VAL_A, getA } from './a.js';
        export const VAL_B = VAL_A * 2 + getA(); // 100 * 2 + 100 = 300
    )");

    dir.writeFile("folder/c.js", R"(
        import { VAL_B } from '../b.js';
        export const VAL_C = VAL_B + 50; // 350
    )");

    std::string mainCode = R"(
        import { VAL_A } from './a.js';
        import { VAL_B } from './b.js';
        import { VAL_C } from './folder/c.js';

        globalThis.loader_res = { a: VAL_A, b: VAL_B, c: VAL_C };
    )";
    dir.writeFile("main.js", mainCode);

    EngineScope scope{engine.get()};

    // Evaluate main source with URL identity correctly parsed by relative loaders
    REQUIRE_NOTHROW(engine->evalModule(String::newString(mainCode), String::newString(dir.getFileUrl("main.js"))));

    auto res = engine->globalThis().get(String::newString("loader_res")).asObject();
    REQUIRE(res.get(String::newString("a")).asNumber().getInt32() == 100);
    REQUIRE(res.get(String::newString("b")).asNumber().getInt32() == 300);
    REQUIRE(res.get(String::newString("c")).asNumber().getInt32() == 350);
}

TEST_CASE("Module: Default Exports Explicit Semantics", "[module]") {
    auto        engine = createModuleEngine();
    EngineScope scope{engine.get()};
    engine->registerModule(ModConstMeta);
    engine->registerModule(ModFuncMeta);
    engine->registerModule(ModLiteralMeta);
    engine->registerModule(ModClassMeta);
    engine->registerModule(ModEnumMeta);

    REQUIRE_NOTHROW(engine->evalModule(String::newString(R"(
        import mod_const1 from 'mod_const';
        import mod_const2 from 'mod_const';
        import mod_func from 'mod_func';
        import mod_literal from 'mod_literal';
        import mod_class from 'mod_class';
        import mod_enum from 'mod_enum';

        globalThis.def_res = {
            const1: mod_const1,
            const2: mod_const2,
            func1: mod_func(),
            func2: mod_func(),
            literal: mod_literal,
            actorId: new mod_class(99).getId(),
            enumIdle: mod_enum.Idle
        };
    )")));

    auto res = engine->globalThis().get(String::newString("def_res")).asObject();

    // Constant default export evaluates purely ONCE during instantiation/import time caching
    REQUIRE(res.get(String::newString("const1")).asNumber().getInt32() == 1);
    REQUIRE(res.get(String::newString("const2")).asNumber().getInt32() == 1);

    // Functional default export invokes its callback every single time JS side calls it
    REQUIRE(res.get(String::newString("func1")).asNumber().getInt32() == 2);
    REQUIRE(res.get(String::newString("func2")).asNumber().getInt32() == 3);

    // Primitive / Constructor exports
    REQUIRE(res.get(String::newString("literal")).asNumber().getInt32() == 42);
    REQUIRE(res.get(String::newString("actorId")).asNumber().getInt32() == 99);
    REQUIRE(res.get(String::newString("enumIdle")).asNumber().getInt32() == 0);
}

TEST_CASE("Module: Namespaced Aliases", "[module]") {
    auto        engine = createModuleEngine();
    EngineScope scope{engine.get()};
    engine->registerModule(NetModuleMeta);

    REQUIRE_NOTHROW(engine->evalModule(String::newString(R"(
        import * as net from 'net';

        const nsList = ['core.net.Connection', 'core.net.Status'];
        for (const ns of nsList) {
            let cur = net;
            const parts = ns.split('.');
            for (let i = 0; i < parts.length - 1; i++) {
                cur = cur[parts[i]];
                if (cur == null) {
                    throw new Error(`Missing namespace: ${parts.slice(0, i + 1).join('.')}`);
                }
            }
        }

        let Conn = net.core.net.Connection;
        let Status = net.core.net.Status;

        globalThis.ns_res = {
            id: new Conn(77).getId(),
            idle: Status.Idle,
            name: Status.$name
        };
    )")));

    auto res = engine->globalThis().get(String::newString("ns_res")).asObject();
    REQUIRE(res.get(String::newString("id")).asNumber().getInt32() == 77);
    REQUIRE(res.get(String::newString("idle")).asNumber().getInt32() == 0);
    REQUIRE(res.get(String::newString("name")).asString().getValue() == "core.net.Status");
}

TEST_CASE("Module: Export Static Class and Namespaced Static Class", "[module]") {
    auto        engine = createModuleEngine();
    EngineScope scope{engine.get()};

    engine->registerModule(StaticClassModuleMeta);

    REQUIRE_NOTHROW(engine->evalModule(String::newString(R"(
        import { MathUtils } from 'static_mod';
        import * as static_mod from 'static_mod';

        globalThis.static_class_res = {
            addRes: MathUtils.add(10, 20),
            piRes: MathUtils.PI
        };

        // Verify whether a static class with a namespace is correctly mapped and exists in the namespace sub-object
        let Logger = static_mod.core.utils.Logger;
        Logger.log("hello from static module");
    )")));

    // 1. Verify the functionality of regular static class (MathUtils) properties and methods
    auto res = engine->globalThis().get(String::newString("static_class_res")).asObject();
    REQUIRE(res.get(String::newString("addRes")).asNumber().getInt32() == 30);
    REQUIRE(res.get(String::newString("piRes")).asNumber().getValueAs<double>() == Catch::Approx(3.14159));

    // 2. Verify the namespace static class (core.utils.Logger) and the side effects of method calls
    auto state = engine->getData<ModState>();
    REQUIRE(state->lastLog == "hello from static module");
}

TEST_CASE("Module: Edge Cases (C++ Exceptions)", "[module]") {
    // Edge Case 1: Checking for C++ layer exceptions properly rejecting duplicate exports
    REQUIRE_THROWS_AS(defModule("err1").exportConstant("A", 1).exportConstant("A", 2).build(), std::logic_error);

    REQUIRE_THROWS_AS(
        defModule("err2").exportFunction("f", []() {}).exportFunction("f", []() {}).build(),
        std::logic_error
    );

    REQUIRE_THROWS_AS(
        defModule("err3").exportClass(ModuleActorMeta).exportClass(ModuleActorMeta).build(),
        std::logic_error
    );

    REQUIRE_THROWS_AS(
        defModule("err4").exportEnum(ModuleStateMeta).exportEnum(ModuleStateMeta).build(),
        std::logic_error
    );
}

TEST_CASE("Module: Edge Cases (Ghost Module)", "[module]") {
    auto        engine = createModuleEngine();
    EngineScope scope{engine.get()};

    // Edge Case 2: Ensure an exception passes right through on absent modules.
    REQUIRE_THROWS_AS(engine->evalModule(String::newString("import 'ghost_module';")), jspp::Exception);
}

TEST_CASE("Module: Edge Cases (Concurrent Imports)", "[module]") {
    auto        engine = createModuleEngine();
    EngineScope scope{engine.get()};
    engine->registerModule(SharedMathMeta);

    // Edge Case 3: Verify importing the same module repeatedly gives exactly the identical namespace
    REQUIRE_NOTHROW(engine->evalModule(String::newString(R"(
        import * as mod1 from 'shared_math';
        import * as mod2 from 'shared_math';
        globalThis.same_ref = (mod1 === mod2);
        globalThis.pi_val = mod1.PI;
    )")));

    REQUIRE(engine->globalThis().get(String::newString("same_ref")).asBoolean().getValue() == true);
    REQUIRE(
        engine->globalThis().get(String::newString("pi_val")).asNumber().getValueAs<double>() == Catch::Approx(3.14159)
    );
}


TEST_CASE("Module: Dynamic Import with Top-Level Await (TLA)", "[module]") {
    auto          engine = createModuleEngine();
    ScopedTempDir dir("jspp_tla_dynamic_import_test");

    // Construct a module that contains a Top-Level Await, and the await is on a Promise controlled by an external C++
    dir.writeFile("tla_module.js", R"(
        export const step1 = "init";
        // Forcibly block here until C++ externally calls resolveTla() to release
        await globalThis.tlaPromise;
        export const step2 = "done";
    )");

    std::string mainCode = R"(
        globalThis.import_resolved_prematurely = false;
        globalThis.import_finished = false;
        globalThis.tla_test_res = null;
        globalThis.tla_resolved = false;

        // Create a controlled Promise
        globalThis.tlaPromise = new Promise((resolve) => {
            globalThis.resolveTla = () => {
                globalThis.tla_resolved = true;
                resolve();
            };
        });

        import('./tla_module.js').then(m => {
            globalThis.import_finished = true;
            if (!globalThis.tla_resolved) {
                // Fatal defect detection: If you enter here, it means the TLA has not yet finished, but import() was resolved prematurely!
                globalThis.import_resolved_prematurely = true;
            }
            globalThis.tla_test_res = {
                s1: m.step1,
                s2: m.step2
            };
        }).catch(e => {
            globalThis.import_err = String(e);
        });
    )";
    dir.writeFile("main.js", mainCode);

    {
        EngineScope scope{engine.get()};
        engine->evalModule(String::newString(mainCode), String::newString(dir.getFileUrl("main.js")));
    }

    {
        EngineScope scope{engine.get()};

        // Check 1: The module is stuck at TLA at this point, import_finished must not be true!
        auto premature = engine->globalThis().get(String::newString("import_resolved_prematurely"));
        REQUIRE(premature.isBoolean());
        // If this assertion fails here, it clearly proves that the engine backend has a TLA semantics-breaking bug
        // (Pending state is passed directly)
        REQUIRE(premature.asBoolean().getValue() == false);

        auto finished = engine->globalThis().get(String::newString("import_finished"));
        REQUIRE(finished.isBoolean());
        REQUIRE(finished.asBoolean().getValue() == false);

        // Check 2: After confirming there are no errors, the C layer actively triggers the TLA to continue execution
        auto resolveTla = engine->globalThis().get(String::newString("resolveTla"));
        REQUIRE(resolveTla.isFunction());
        resolveTla.asFunction().call(engine->globalThis());
    }

    // For engine backends that require manually pumping out microtasks (such as QuickJS), use macro isolation execution
    // This will handle the subsequent task chain generated after resolveTla()
#ifdef JSPP_BACKEND_QUICKJS
    engine->pumpPendingJobs();
    engine->getJobQueue()->shutdown(true);
    engine->getJobQueue()->loopAndWait();
#endif

    {
        EngineScope scope{engine.get()};

        auto err = engine->globalThis().get(String::newString("import_err"));
        if (!err.isUndefined() && !err.isNull()) {
            FAIL("JS Error: " << err.asString().getValue());
        }

        // Check 3: Final import completed successfully, export variables extracted normally
        auto finished2 = engine->globalThis().get(String::newString("import_finished"));
        REQUIRE(finished2.isBoolean());
        REQUIRE(finished2.asBoolean().getValue() == true);

        auto res = engine->globalThis().get(String::newString("tla_test_res")).asObject();
        REQUIRE(res.get(String::newString("s1")).asString().getValue() == "init");
        REQUIRE(res.get(String::newString("s2")).asString().getValue() == "done");
    }
}

TEST_CASE("Module: Cyclic Import", "[module]") {
    auto          engine = createModuleEngine();
    ScopedTempDir dir("jspp_cyclic_test");

    dir.writeFile("a.js", R"(
        import { b } from './b.js';
        export const a = b + 1;
    )");
    dir.writeFile("b.js", R"(
        import { a } from './a.js';
        export const b = a + 1;
    )");

    {
        EngineScope scope{engine.get()};

        // This import will throw a ReferenceError (TDZ) at the engine level
        REQUIRE_THROWS_AS(
            engine->evalModule(String::newString("import './a.js';"), String::newString(dir.getFileUrl("main.js"))),
            jspp::Exception
        );
    }
}

} // namespace ut