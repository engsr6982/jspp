#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/ValueHelper.h"
#include <node.h>

// ---------------------------------------------------------
// C++ Business Logic and JS Binding Declaration
// ---------------------------------------------------------

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
    static jspp::ModuleMeta const m =
        jspp::binding::defModule(name).exportFunction("fib", &fib).exportClass(kVec3Meta).build();
    return m;
}


// ---------------------------------------------------------
// 2. Node.js Addon Lifecycle and Initialization
// ---------------------------------------------------------

// Callback when Node.js environment is destroyed, safely destruct Engine
void CleanupEngine(void* arg) {
    auto engine = static_cast<jspp::Engine*>(arg);
    delete engine;
}


NODE_MODULE_INIT(/* exports, module, context */) {
    v8::Isolate* isolate = context->GetIsolate();

    auto engine = new jspp::Engine(isolate, context, jspp::v8_backend::V8InitializeFlags::AddonMode);

    // Binding Node.js environment cleanup hooks
    node::AddEnvironmentCleanupHook(isolate, CleanupEngine, engine);

    jspp::EngineScope scope(engine);

    // Convert the underlying v8::Local<v8::Object> to a jspp wrapped type
    auto        exportsObj = jspp::ValueHelper::wrap<jspp::Object>(exports);
    auto const& meta       = buildModule("example-node-addon");


    for (const auto& f : meta.functions_) {
        auto func = jspp::Function::newFunction([cb = f.callback_](jspp::Arguments const& args) { return cb(args); });
        exportsObj.set(jspp::String::newString(f.name_), func);
    }

    for (auto c : meta.classes_) {
        auto ctor = engine->performRegisterClass(*c);
        exportsObj.set(jspp::String::newString(c->name_), ctor);
    }

    for (auto e : meta.enums_) {
        auto enumObj = engine->performRegisterEnum(*e);
        exportsObj.set(jspp::String::newString(e->name_), enumObj);
    }

    for (const auto& v : meta.constants_) {
        auto val = v.getter_();
        exportsObj.set(jspp::String::newString(v.name_), val);
    }
}