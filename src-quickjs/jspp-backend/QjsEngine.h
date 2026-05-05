#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Fwd.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string_view>
#include <unordered_map>

JSPP_WARNING_GUARD_BEGIN
#include "quickjs.h"
JSPP_WARNING_GUARD_END

namespace jspp {
struct ModuleFunctionExport;
struct ModuleConstantExport;
struct EnumMeta;
struct ClassMeta;
struct ModuleMeta;
} // namespace jspp


namespace jspp::qjs_backend {

class JobQueue;

enum class QjsInitializeFlags {
    Normal = 0,

    /** @brief Disable the built-in ES module loader. */
    NoModuleLoader = 1 << 0,

    /**
     * @brief Allow the loader to resolve and load native shared libraries (.so, .dll).
     * @note if set NoModuleLoader, this flag will be ignored.
     */
    EnableQuickJsCModuleSupport = 1 << 1,

    /** @brief Do not call JS_FreeRuntime on destruction (Host owns the JSRuntime). */
    NoFreeRuntime = 1 << 2,

    /** @brief Do not call JS_FreeContext on destruction (Host owns the JSContext). */
    NoFreeContext = 1 << 3,

    /**
     * @brief Disable automatic posting of pending jobs to the jspp JobQueue.
     * @note If set, pumpPendingJobs() will return early. Use this if the host
     *       handles JS_ExecutePendingJob or if you want to avoid using the internal JobQueue.
     */
    NoJobQueuePosting = 1 << 4,

    /** @brief Disable automatic garbage collection in QjsEngine::dispose */
    NoGcInDispose = 1 << 5,

    /** @brief Combination for safe embedding in existing QuickJS environments. */
    AddonMode = NoModuleLoader | NoFreeRuntime | NoFreeContext | NoJobQueuePosting | NoGcInDispose
};
JSPP_DECL_FLAG_OPERATOR(QjsInitializeFlags);

class QjsEngine {
public:
    JSPP_DISABLE_COPY(QjsEngine);

    using JSRuntimeFactory = std::function<JSRuntime*()>;

    /**
     * Create an engine instance.
     * @param factory If a factory function is passed, the user-provided JSRuntime* will be used.
     * @param flags Initialize flags.
     * @note if you do not want jspp to release JSRuntime*, please set the flag.
     */
    explicit QjsEngine(
        JSRuntimeFactory const& factory = nullptr,
        QjsInitializeFlags      flags   = QjsInitializeFlags::Normal
    );

    /**
     * Create an engine using an external instance.
     * @param runtime The JSRuntime* instance.
     * @param context The JSContext* instance.
     * @param flags Initialize flags.
     * @note This overload is usually used in addon scenarios.
     */
    explicit QjsEngine(
        JSRuntime*         runtime,
        JSContext*         context,
        QjsInitializeFlags flags = QjsInitializeFlags::AddonMode
    );

protected:
    ~QjsEngine();

    void dispose();

public:
    // qjs backend specific
    [[nodiscard]] JSRuntime* runtime() const;
    [[nodiscard]] JSContext* context() const;

    void pumpPendingJobs(); // if set NoJobQueuePosting, this function will return early.

    [[nodiscard]] JobQueue* getJobQueue() const;

    Local<Value> loadByteCode(std::filesystem::path const& path, bool main = false);

protected:
    [[nodiscard]] Engine*       asEngine();
    [[nodiscard]] Engine const* asEngine() const;

    void initContext();

    void setToStringTag(Local<Object>& obj, std::string_view name);

    [[nodiscard]] JSValue newPointerData(void* ptr) const;

    using DataFunctionCallback = Local<Value> (*)(Arguments const& args, void* data1, void* data2);
    Local<Function> newDataFunction(DataFunctionCallback callback, void* data1, void* data2);

    Local<Function> newConstructor(ClassMeta const& meta);

    void          buildStaticMembers(Local<Object>& obj, ClassMeta const& meta);
    Local<Object> newClassPrototype(ClassMeta const& meta);

    // ====== Native Class ======
    static void NativeClassFinalizer(JSRuntime* runtime, JSValueConst value);
    static void NativeClassGcMarker(JSRuntime* runtime, JSValueConst value, JS_MarkFunc* markFunc);
    static bool CheckPrototypeSignature(ClassMeta const* meta, ClassMeta const* target);

    friend class QjsEngineScope;
    friend class QjsExitEngineScope;
    friend class QjsStackFrameScope;
    friend struct QjsHelper;
    friend Function;
    friend Arguments;
    friend struct QjsModuleLoader;
    friend class PauseGcGuard;

    JSRuntime*               runtime_{nullptr};
    JSContext*               context_{nullptr};
    QjsInitializeFlags const flags_{};

    mutable std::recursive_mutex mutex_{};

    std::unique_ptr<JobQueue> queue_{nullptr};

    int              pauseGcCount_{0};
    std::atomic_bool pumpScheduled_{false};

    JSClassID functionDataClassId_{JS_INVALID_CLASS_ID}; // for Function::newFunction
    JSClassID pointerDataClassId_{JS_INVALID_CLASS_ID};  // for QjsEngine::newPointerData

    // Mark C++ as a bound function to prevent an infinite loop when JS dispatches an override
    JSAtom nativeFunctionTag_{};
    JSAtom toStringTagSymbolAtom_{};
    JSAtom referenceInternalAtom_{};

    std::unordered_map<ClassMeta const*, JSClassID>                   classIds_{};
    std::unordered_map<ClassMeta const*, std::pair<JSValue, JSValue>> classConstructors_{}; // ctor, prototype
    std::unordered_map<ClassMeta const*, JSValue>                     staticClassObject_{}; // static class only

    std::unordered_map<EnumMeta const*, Global<Object>> enumObject_{}; // for native module lazy load

    std::unordered_map<JSModuleDef*, ModuleMeta const*> module2meta_{}; // fast lookup, avoid AtomToCString
};

struct QjsModuleLoader {
    QjsModuleLoader() = delete;

    inline static constexpr std::string_view kFileUrlPrefix = "file://";
    inline static constexpr std::string_view kDefaultExport = "default";

    /**
     * Set module import metadata. If std::nullopt is passed, the current value is not modified.
     * @note Return false if an exception occurs
     */
    [[nodiscard]] static bool setImportMeta(
        JSContext*                 ctx,
        JSModuleDef*               def,
        std::optional<std::string> url,
        std::optional<bool>        main = std::nullopt
    );

    // === QuickJS Engine Callback ===
    static char* normalize(JSContext* ctx, const char* importingModule, const char* importSpecifier, void* opaque);
    static JSModuleDef* loader(JSContext* ctx, const char* specifier, void* opaque, JSValueConst attr);
    static int          attrChecker(JSContext* ctx, void* opaque, JSValueConst attr);

    enum class ImportAttrType { Unknown = 0, Js, Json };
    static ImportAttrType resolveImportAttr(JSContext* ctx, JSValueConst attr);

    using QjsCModuleInitFunc                   = JSModuleDef* (*)(JSContext* ctx, const char* specifier);
    inline static constexpr auto kCModuleEntry = "js_init_module";
    static JSModuleDef* performLoadCModule(Engine* engine, std::filesystem::path const& path, const char* specifier);
    static JSModuleDef* performLoadJsonModule(Engine* engine, std::filesystem::path const& path, const char* specifier);
    static JSModuleDef*
    performLoadByteCodeModule(Engine* engine, std::filesystem::path const& path, const char* specifier);
    static JSModuleDef*
    performLoadScriptModule(Engine* engine, std::filesystem::path const& path, const char* specifier);

    // ====== Native Module ======
    [[nodiscard]] static JSModuleDef* performNewNativeModule(Engine* engine, ModuleMeta const* meta);
    static void performDeclareModuleExports(Engine* engine, JSModuleDef* def, ModuleMeta const* meta);
    static int  performModuleExports(Engine* engine, JSModuleDef* def);
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