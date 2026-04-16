#include "catch2/benchmark/catch_benchmark.hpp"
#include "catch2/catch_test_macros.hpp"
#include "jspp/Jspp.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/binding/TypeConverter.h"

#ifdef JSPP_BACKEND_V8
#include "v8.h"
#endif

#ifdef JSPP_BACKEND_QUICKJS
#include "jspp-backend/QjsHelper.h"
#include "quickjs.h"
#endif

using namespace jspp;
using namespace jspp::binding;

// =========================================================
// C++ Target Function
// =========================================================
static int native_add(int a, int b) { return a + b; }

// =========================================================
// jspp Binding
// =========================================================
auto BenchMeta = jspp::binding::defClass<void>("BenchMath").func("add", &native_add).build();


// =========================================================
// Raw Native Binding
// =========================================================
#ifdef JSPP_BACKEND_V8
static void raw_v8_add(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto ctx     = isolate->GetCurrentContext();
    int  a       = info[0]->Int32Value(ctx).FromMaybe(0);
    int  b       = info[1]->Int32Value(ctx).FromMaybe(0);
    info.GetReturnValue().Set(a + b);
}
#endif

#ifdef JSPP_BACKEND_QUICKJS
static JSValue raw_qjs_add(JSContext* ctx, JSValueConst /*this_val*/, int /*argc*/, JSValueConst* argv) {
    int32_t a = 0, b = 0;
    JS_ToInt32(ctx, &a, argv[0]);
    JS_ToInt32(ctx, &b, argv[1]);
    return JS_NewInt32(ctx, a + b);
}
#endif


TEST_CASE("Benchmark: C++ to JS Bridge Overhead", "[benchmark]") {
    auto engine = std::make_unique<Engine>();
    EngineScope scope{engine.get()};

    // Register jspp binding
    engine->registerClass(BenchMeta);

    // Register Raw Native binding
#ifdef JSPP_BACKEND_V8
    auto isolate = engine->isolate();
    auto ctx     = engine->context();
    auto rawFn   = v8::FunctionTemplate::New(isolate, raw_v8_add)->GetFunction(ctx).ToLocalChecked();
    ctx->Global()->Set(ctx, v8::String::NewFromUtf8Literal(isolate, "raw_add"), rawFn).Check();
#endif

#ifdef JSPP_BACKEND_QUICKJS
    auto ctx    = qjs_backend::QjsHelper::currentContextChecked();
    auto rawFn  = JS_NewCFunction(ctx, raw_qjs_add, "raw_add", 2);
    auto global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "raw_add", rawFn);
    JS_FreeValue(ctx, global);
#endif

    // =========================================================
    // Prepare JavaScript functions for benchmarking
    // =========================================================

    // JS Pure Math (Baseline)
    auto pure_js_script = String::newString(R"(
        function js_add(a, b) { return a + b; }
        (function run_pure_js() {
            let sum = 0;
            for (let i = 0; i < 10000; i++) {
                sum += js_add(1, i);
            }
            return sum;
        })
    )");
    auto pure_js_fn     = engine->evalScript(pure_js_script).asFunction();

    // Raw Native Bridge
    auto raw_script = String::newString(R"(
        (function run_raw() {
            let sum = 0;
            for (let i = 0; i < 10000; i++) {
                sum += raw_add(1, i);
            }
            return sum;
        })
    )");
    auto raw_fn     = engine->evalScript(raw_script).asFunction();

    // jspp Bridge
    auto jspp_script = String::newString(R"(
        (function run_jspp() {
            let sum = 0;
            for (let i = 0; i < 10000; i++) {
                sum += BenchMath.add(1, i);
            }
            return sum;
        })
    )");
    auto jspp_fn     = engine->evalScript(jspp_script).asFunction();


    // =========================================================
    // Execute Benchmarks (Catch2 handles the warmup and iterations)
    // =========================================================

    BENCHMARK("Pure JavaScript loop (10,000 calls)") {
        return pure_js_fn.call(engine->globalThis()); //
    };

    BENCHMARK("Raw Native Binding loop (10,000 calls)") {
        return raw_fn.call(engine->globalThis()); //
    };

    BENCHMARK("jspp Binding loop (10,000 calls)") {
        return jspp_fn.call(engine->globalThis()); //
    };
}
