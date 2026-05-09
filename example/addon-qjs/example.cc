#include "quickjs.h"

#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/core/Engine.h"


int fib(int n) {
    int a = 0, b = 1;
    for (int i = 0; i < n; i++) {
        int temp = a + b;
        a        = b;
        b        = temp;
    }
    return a;
}

class Vec3 {
public:
    float x{}, y{}, z{};

    Vec3(float x, float y, float z) : x(x), y(y), z(z) {}
};

auto const kVec3Meta = jspp::binding::defClass<Vec3>("Vec3")
                           .ctor<float, float, float>()
                           .prop("x", &Vec3::x)
                           .prop("y", &Vec3::y)
                           .prop("z", &Vec3::z)
                           .build();

jspp::ModuleMeta const& buildModule(std::string_view name) {
    // In the Addon scenario, it itself is a module, so it can be safely cached here.
    static jspp::ModuleMeta const m =
        jspp::binding::defModule(name).exportFunction("fib", &fib).exportClass(kVec3Meta).build();
    return m;
}


// ============================================================
// activeEngines map
// ============================================================
// Why this map is needed:
//
// The host may hold N JSContext instances (e.g. 1 Runtime + N
// Contexts, or N independent VMs). The first time *each* Context
// imports this DLL, QuickJS's per-Context module cache is empty for
// this module name, so js_init_module is invoked again with a
// different JSContext*.
//
// Every js_init_module call requires its own jspp::Engine instance
// bound to that specific JSContext.  This map tracks which
// JSContext already has a live Engine so we reuse it if the same
// Context triggers multiple imports (e.g. different modules
// importing the same DLL).
//
// When this map can be omitted:
//
// If the host guarantees exactly **one** JSContext for the entire
// process lifetime (Scenario 1: 1 Runtime + 1 Context), the map is
// unnecessary because js_init_module will only ever be called once
// per DLL.  The Engine can simply be stored in a global/static
// variable and cleaned up via atexit or a module cleanup hook.
//
// In all other scenarios the map is required for correctness.
// ============================================================
static std::unordered_map<JSContext*, jspp::Engine*> g_activeEngines;
static std::mutex                                    g_mutex; // for g_activeEngines

jspp::Engine* getOrCreateEngine(JSContext* ctx) {
    std::scoped_lock lock(g_mutex);

    auto iter = g_activeEngines.find(ctx);
    if (iter == g_activeEngines.end()) {
        auto rt     = JS_GetRuntime(ctx);
        auto engine = new jspp::Engine{rt, ctx, jspp::qjs_backend::QjsInitializeFlags::AddonMode};
        iter        = g_activeEngines.emplace(ctx, engine).first;
    }
    return iter->second;
}
JSClassID newCleanupClass(JSContext* ctx) {
    auto rt = JS_GetRuntime(ctx);

    JSClassID  id{JS_INVALID_CLASS_ID};
    JSClassDef def{};
    def.class_name = "__engine_cleanup_hook__";
    def.finalizer  = [](JSRuntime* /*rt*/, JSValueConst val) {
        auto id = JS_GetClassID(val);
        if (auto engine = static_cast<jspp::Engine*>(JS_GetOpaque(val, id))) {
            {
                std::scoped_lock lock(g_mutex);
                g_activeEngines.erase(engine->context());
            }
            delete engine;
        }
    };
    JS_NewClassID(rt, &id);
    JS_NewClass(rt, id, &def);
    assert(id != JS_INVALID_CLASS_ID);
    return id;
}
void addCleanupHook(JSContext* ctx, JSClassID id, JSModuleDef* def, jspp::Engine* engine) {
    auto v = JS_NewObjectClass(ctx, id);
    JS_SetOpaque(v, engine);

    // Note: jspp has used ModulePrivateData to transmit the engine pointer, you cannot change it.
    auto priv = JS_GetModulePrivateValue(ctx, def);
    JS_DefinePropertyValueStr(ctx, priv, "__cleanup_for_engine__", v, 0);
    JS_FreeValue(ctx, priv);
}

#ifdef _WIN32
#define JS_MODULE_EXPORT __declspec(dllexport)
#else
#define JS_MODULE_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {
// Note: The signature of the entry function needs to match the requirements of quickjs-libc.c
JS_MODULE_EXPORT JSModuleDef* js_init_module(JSContext* ctx, const char* module_name) {
    if (auto engine = getOrCreateEngine(ctx)) {
        auto id = newCleanupClass(ctx);
        // NOTE: Module caching is handled by QuickJS's per-Context
        // loaded_modules list.  js_init_module is only invoked on a
        // cache miss, so we do not need to implement our own cache
        // layer here.
        auto module = jspp::qjs_backend::QjsModuleLoader::performNewNativeModule(engine, &buildModule(module_name));
        addCleanupHook(ctx, id, module, engine);
        return module;
    }
    return nullptr;
}
}
