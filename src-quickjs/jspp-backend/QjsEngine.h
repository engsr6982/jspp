#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Fwd.h"

JSPP_WARNING_GUARD_BEGIN
#include "quickjs.h"
JSPP_WARNING_GUARD_END

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace jspp {
struct ClassMeta;
}


namespace jspp::qjs_backend {

class JobQueue;

class QjsEngine {
public:
    JSPP_DISABLE_COPY(QjsEngine);

    explicit QjsEngine();

protected:
    ~QjsEngine();

    void dispose();

public:
    // qjs backend specific
    void pumpPendingJobs();

    Local<Value> loadByteCode(std::filesystem::path const& path, bool main = false);

protected:
    [[nodiscard]] Engine*       asEngine();
    [[nodiscard]] Engine const* asEngine() const;

    void setToStringTag(Local<Object>& obj, std::string_view name);

    using DataFunctionCallback = Local<Value> (*)(Arguments const& args, void* data1, void* data2);
    Local<Function> newDataFunction(DataFunctionCallback callback, void* data1, void* data2);

    Local<Function> newConstructor(ClassMeta const& meta);

    void          buildStaticMembers(Local<Object>& obj, ClassMeta const& meta);
    Local<Object> newClassPrototype(ClassMeta const& meta);

    static void NativeClassFinalizer(JSRuntime* runtime, JSValueConst value);
    static void NativeClassGcMarker(JSRuntime* runtime, JSValueConst value, JS_MarkFunc* markFunc);

    friend class QjsEngineScope;
    friend class QjsExitEngineScope;
    friend class QjsStackFrameScope;
    friend struct QjsHelper;
    friend Function;
    friend Arguments;
    friend class PauseGcGuard;

    JSRuntime* runtime_{nullptr};
    JSContext* context_{nullptr};

    mutable std::recursive_mutex mutex_{};

    std::unique_ptr<JobQueue> queue_{nullptr};

    int              pauseGcCount_{0};
    std::atomic_bool pumpScheduled_{false};

    JSClassID functionDataClassId_{}; // for Function::newFunction
    JSClassID pointerDataClassId_{};

    // Mark C++ as a bound function to prevent an infinite loop when JS dispatches an override
    JSAtom nativeFunctionTag_{};
    JSAtom toStringTagSymbolAtom_{};
    JSAtom referenceInternalAtom_{};

    std::unordered_map<ClassMeta const*, JSClassID>                   classIds_{};
    std::unordered_map<ClassMeta const*, std::pair<JSValue, JSValue>> classConstructors_{}; // ctor, proto
};

class PauseGcGuard {
    Engine* engine_;

public:
    JSPP_DISABLE_COPY_MOVE(PauseGcGuard);
    JSPP_DISABLE_NEW();
    explicit PauseGcGuard(Engine* engine);
    ~PauseGcGuard();
};


} // namespace jspp::qjs_backend