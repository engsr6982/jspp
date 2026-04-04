#pragma once
#include "Fwd.h"
#include "jspp/Macro.h"

#include <filesystem>
#include <typeindex>
#include <unordered_map>

namespace jspp {

struct ClassMeta; // forward declaration
struct EnumMeta;
class ITrackedHandle;
namespace internal {
class V8EscapeScope;
} // namespace internal

class Engine {
public:
    JSPP_DISABLE_COPY(Engine);

    Engine(Engine&&) noexcept            = default;
    Engine& operator=(Engine&&) noexcept = default;

    ~Engine();

    explicit Engine();

    /**
     * To create a Js engine, using sources from outside is isolate and context.
     * This overload is commonly used in NodeJs Addons.
     * When using isolate and contexts from outside (e.g. NodeJs), the Platform is not required.
     */
    explicit Engine(v8::Isolate* isolate, v8::Local<v8::Context> context);

    [[nodiscard]] v8::Isolate* isolate() const;

    [[nodiscard]] v8::Local<v8::Context> context() const;

    void setData(std::shared_ptr<void> data);

    template <typename T>
    [[nodiscard]] inline std::shared_ptr<T> getData() const {
        return std::static_pointer_cast<T>(userData_);
    }

    [[nodiscard]] bool isDestroying() const;

    Local<Value> eval(Local<String> const& code);

    Local<Value> eval(Local<String> const& code, Local<String> const& source);

    void loadFile(std::filesystem::path const& path);

    void gc() const;

    [[nodiscard]] Local<Object> globalThis() const;

    /**
     * Add a managed resource to the engine.
     * The managed resource will be destroyed when the engine is destroyed.
     * @param resource Resources that need to be managed
     * @param value The v8 object associated with this resource.
     * @param deleter The deleter function to be called when the resource is destroyed.
     */
    void addManagedResource(void* resource, v8::Local<v8::Value> value, std::function<void(void*)>&& deleter);

    /**
     * Register a binding class and mount it to globalThis
     */
    Local<Function> registerClass(ClassMeta const& meta);

    Local<Object> registerEnum(EnumMeta const& meta);

    /**
     * Get metadata of the registered instance class
     * @note If it is a static class, always return nullptr (e.g. defClass<void>)
     */
    [[nodiscard]] ClassMeta const* getClassMeta(std::type_index typeId) const;

    Local<Object> newInstance(ClassMeta const& meta, std::unique_ptr<NativeInstance>&& instance);

    [[nodiscard]] bool isInstanceOf(Local<Object> const& obj, ClassMeta const& meta) const;

    [[nodiscard]] InstancePayload* getInstancePayload(Local<Object> const& obj) const;

    [[nodiscard]] bool trySetReferenceInternal(Local<Object> const& parentObj, Local<Object> const& subObj);

private:
    void setToStringTag(v8::Local<v8::FunctionTemplate>& obj, std::string_view name, bool hasConstructor);
    void setToStringTag(v8::Local<v8::Object>& obj, std::string_view name);

    v8::Local<v8::FunctionTemplate> newConstructor(ClassMeta const& meta);

    void buildStaticMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta);
    void buildInstanceMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta);

    void addTrackedHandle(ITrackedHandle* handle);
    void removeTrackedHandle(ITrackedHandle* handle);

    friend EngineScope;
    friend ExitEngineScope;
    friend internal::V8EscapeScope;
    friend class ITrackedHandle;
    friend class enable_trampoline;

    template <typename>
    friend class Global;
    template <typename>
    friend class Weak;

    struct ManagedResource {
        Engine*                    runtime;
        void*                      resource;
        std::function<void(void*)> deleter;
    };

    enum class InternalFieldSolt : int {
        InstancePayload    = 0, // for InstancePayload(managed Engine、NativeInstance、any flag)
        ParentClassThisRef = 1, // for ReturnValuePolicy::kReferenceInternal
        Count,
    };

    v8::Isolate*            isolate_{nullptr};
    v8::Global<v8::Context> context_{};
    std::shared_ptr<void>   userData_{nullptr};

    bool       isDestroying_{false};
    bool const isExternalIsolate_{false};

    ITrackedHandle* trackedHead_{nullptr};

    // This symbol is used to mark the construction of objects from C++ (with special logic).
    v8::Global<v8::Symbol> constructorSymbol_{};

    // Mark C++ as a bound function to prevent an infinite loop when JS dispatches an override
    v8::Global<v8::Private> nativeFunctionTag_{};

    std::unordered_map<ManagedResource*, v8::Global<v8::Value>>            managedResources_;
    std::unordered_map<std::string, ClassMeta const*>                      registeredClasses_;
    std::unordered_map<ClassMeta const*, v8::Global<v8::FunctionTemplate>> classConstructors_;

    std::unordered_map<std::type_index, ClassMeta const*> instanceClassMapping;

    std::unordered_map<std::string, EnumMeta const*> registeredEnums_;
};


} // namespace jspp