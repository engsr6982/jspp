#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Fwd.h"

JSPP_WARNING_GUARD_BEGIN
#include <v8-local-handle.h>
#include <v8.h>
JSPP_WARNING_GUARD_END

namespace jspp {
struct ModuleMeta;
struct ClassMeta;
struct EnumMeta;
} // namespace jspp

namespace jspp::v8_backend {

enum class V8InitializeFlags {
    Normal = 0,

    /** @brief Disable the built-in ES module loader. */
    NoModuleLoader = 1 << 0,

    /** @brief Do not call isolate->Dispose() on destruction (Host owns the Isolate). */
    NoDisposeIsolate = 1 << 1,

    /** @brief Do not use Context embedder data slots. */
    NoContextData = 1 << 2,

    /** @brief Combination for safe embedding in Node.js or existing V8 environments. */
    AddonMode = NoModuleLoader | NoDisposeIsolate | NoContextData
};
JSPP_DECL_FLAG_OPERATOR(V8InitializeFlags)

class V8Engine {
public:
    JSPP_DISABLE_COPY(V8Engine);

    using V8IsolateFactory = std::function<v8::Isolate*()>;

    /**
     * Create an engine instance
     * @note If no factory function is provided, isolation is created automatically.
     */
    explicit V8Engine(V8IsolateFactory const& factory = nullptr, V8InitializeFlags flags = V8InitializeFlags::Normal);

    /**
     * To create a Js engine, using sources from outside is isolate and context.
     * This overload is commonly used in Node.Js Addons.
     * When using isolate and contexts from outside (e.g. Node.Js), the Platform is not required.
     */
    explicit V8Engine(
        v8::Isolate*           isolate,
        v8::Local<v8::Context> ctx,
        V8InitializeFlags      flags = V8InitializeFlags::AddonMode
    );

protected:
    ~V8Engine();

    void dispose();

public: // v8 backend specific
    [[nodiscard]] v8::Isolate* isolate() const;

    [[nodiscard]] v8::Local<v8::Context> context() const;

    /**
     * Add a managed resource to the engine.
     * The managed resource will be destroyed when the engine is destroyed.
     * @param resource Resources that need to be managed
     * @param value The v8 object associated with this resource.
     * @param deleter The deleter function to be called when the resource is destroyed.
     */
    void addManagedResource(void* resource, v8::Local<v8::Value> value, std::function<void(void*)>&& deleter);

protected:
    [[nodiscard]] Engine*       asEngine();
    [[nodiscard]] Engine const* asEngine() const;

    static void ensureInitializeFlags(V8InitializeFlags flags);
    void        initContext();

    void setToStringTag(v8::Local<v8::FunctionTemplate>& obj, std::string_view name, bool hasConstructor);
    void setToStringTag(v8::Local<v8::Object>& obj, std::string_view name);

    enum class ExternalPairInternalField : int { kData = 0, kEngine = 1, Count };
    static v8::Local<v8::Object>     newExternalPair(void* data, Engine* engine);
    static std::pair<void*, Engine*> extractExternalPair(v8::Local<v8::Value> val);

    v8::Local<v8::FunctionTemplate> newConstructor(ClassMeta const& meta);

    void buildStaticMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta);
    void buildInstanceMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta);

    friend class V8EngineScope;
    friend class V8ExitEngineScope;
    friend class V8EscapeScope;
    friend struct V8Helper;
    friend struct V8ModuleLoader;

    struct ManagedResource {
        Engine*                    runtime{};
        void*                      resource{};
        std::function<void(void*)> deleter;
    };

    enum class InternalFieldSolt : int {
        InstancePayload    = 0, // for InstancePayload(managed Engine、NativeInstance、any flag)
        ParentClassThisRef = 1, // for ReturnValuePolicy::kReferenceInternal
        Count,
    };

    v8::Isolate*            isolate_{nullptr};
    v8::Global<v8::Context> context_{};

    V8InitializeFlags const flags_{V8InitializeFlags::Normal};

    // This symbol is used to mark the construction of objects from C++ (with special logic).
    v8::Global<v8::Symbol> constructorSymbol_{};

    // Mark C++ as a bound function to prevent an infinite loop when JS dispatches an override
    v8::Global<v8::Private> nativeFunctionTag_{};

    v8::Global<v8::ObjectTemplate> externalPairTemplate_{}; // for newExternalPair

    std::unordered_map<ManagedResource*, v8::Global<v8::Value>>            managedResources_;
    std::unordered_map<ClassMeta const*, v8::Global<v8::FunctionTemplate>> classConstructors_;

    std::unordered_map<std::string, v8::Global<v8::Module>> loadedModules_;
    std::unordered_map<int, std::string>                    moduleIdentityMap_; // Module::GetIdentityHash() -> URL
    std::unordered_map<int, ModuleMeta const*> syntheticModules_; // Module::GetIdentityHash() -> ModuleMeta
    std::unordered_map<EnumMeta const*, v8::Global<v8::Object>> enumObject_;
};

struct V8ModuleLoader {
    V8ModuleLoader() = delete;

    static v8::MaybeLocal<v8::Promise> HostImportModuleDynamicallyCallback(
        v8::Local<v8::Context>    context,
        v8::Local<v8::Data>       hostDefinedOptions,
        v8::Local<v8::Value>      resourceName,
        v8::Local<v8::String>     specifier,
        v8::Local<v8::FixedArray> importAttributes
    );
    static void HostInitializeImportMetaObjectCallback(
        v8::Local<v8::Context> context,
        v8::Local<v8::Module>  module,
        v8::Local<v8::Object>  meta
    );
    static v8::MaybeLocal<v8::Module> ResolveModuleCallback(
        v8::Local<v8::Context>    context,
        v8::Local<v8::String>     specifier,
        v8::Local<v8::FixedArray> importAttributes,
        v8::Local<v8::Module>     referrer
    );
    static std::string normalizePath(const std::string& importingModule, const std::string& importSpecifier);
    static v8::MaybeLocal<v8::Module> performLoadFileModule(Engine* engine, const std::string& resolvedUrl);
    static v8::Local<v8::Module>      performNewNativeModule(Engine* engine, ModuleMeta const* meta);
    static v8::MaybeLocal<v8::Value>
    SyntheticModuleEvaluationSteps(v8::Local<v8::Context> context, v8::Local<v8::Module> module);
};

} // namespace jspp::v8_backend