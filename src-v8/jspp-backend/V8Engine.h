#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Fwd.h"

namespace jspp {
struct ClassMeta;
}

JSPP_WARNING_GUARD_BEGIN
#include <v8-local-handle.h>
#include <v8.h>
JSPP_WARNING_GUARD_END

namespace jspp::v8_backend {


class V8Engine {
public:
    JSPP_DISABLE_COPY(V8Engine);

protected:
    /**
     * Create an engine instance, internally automatically new Isolate and Context
     */
    explicit V8Engine(); // todo: support isolate factory callback

    /**
     * To create a Js engine, using sources from outside is isolate and context.
     * This overload is commonly used in NodeJs Addons.
     * When using isolate and contexts from outside (e.g. NodeJs), the Platform is not required.
     */
    explicit V8Engine(v8::Isolate* isolate, v8::Local<v8::Context> ctx);

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

    void setToStringTag(v8::Local<v8::FunctionTemplate>& obj, std::string_view name, bool hasConstructor);
    void setToStringTag(v8::Local<v8::Object>& obj, std::string_view name);

    v8::Local<v8::FunctionTemplate> newConstructor(ClassMeta const& meta);

    void buildStaticMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta);
    void buildInstanceMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta);

    friend class V8EngineScope;
    friend class V8ExitEngineScope;
    friend class V8EscapeScope;
    friend struct V8Helper;

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

    bool const isExternalIsolate_{false};

    // This symbol is used to mark the construction of objects from C++ (with special logic).
    v8::Global<v8::Symbol> constructorSymbol_{};

    // Mark C++ as a bound function to prevent an infinite loop when JS dispatches an override
    v8::Global<v8::Private> nativeFunctionTag_{};

    std::unordered_map<ManagedResource*, v8::Global<v8::Value>>            managedResources_;
    std::unordered_map<ClassMeta const*, v8::Global<v8::FunctionTemplate>> classConstructors_;
};


} // namespace jspp::v8_backend