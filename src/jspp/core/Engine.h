#pragma once
#include "Fwd.h"
#include "jspp/Macro.h"

#include <filesystem>
#include <typeindex>
#include <unordered_map>

#include "jspp-backend/traits/TraitEngine.h"

namespace jspp {

struct ClassMeta; // forward declaration
struct EnumMeta;
class ITrackedHandle;

using BackendEngineImpl = internal::ImplType<Engine>::type;

class Engine final : public BackendEngineImpl {
public:
    JSPP_DISABLE_COPY(Engine);

    using BackendEngineImpl::BackendEngineImpl;

    ~Engine();

    // Engine(Engine&&) noexcept            = default;
    // Engine& operator=(Engine&&) noexcept = default;

    void setData(std::shared_ptr<void> data);

    template <typename T>
    [[nodiscard]] inline std::shared_ptr<T> getData() const {
        return std::static_pointer_cast<T>(userData_);
    }

    [[nodiscard]] bool isDestroying() const;

    Local<Value> evalScript(Local<String> const& code);

    Local<Value> loadFile(std::filesystem::path const& path);

    Local<Value> evalScript(Local<String> const& code, Local<String> const& source);

    void gc();

    [[nodiscard]] Local<Object> globalThis() const;

    /**
     * Register a class to globalThis
     * @note Supports namespace separation, with the delimiter being '.'
     * @return if the class is static class, return object, else return class constructor
     */
    Local<Value> registerClass(ClassMeta const& meta);

    /**
     * Register an enum to globalThis.
     * @note Supports namespace separation, with the delimiter being '.'
     */
    Local<Object> registerEnum(EnumMeta const& meta);

    Local<Value> performRegisterClass(ClassMeta const& meta);

    Local<Object> performRegisterEnum(EnumMeta const& meta);

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
    void initClassTrampoline(InstancePayload const* payload, Local<Object> thiz);

    void addTrackedHandle(ITrackedHandle* handle);
    void removeTrackedHandle(ITrackedHandle* handle);

    friend BackendEngineImpl;

    friend EngineScope;
    friend ExitEngineScope;
    friend StackFrameScope;
    friend Exception;
    friend class ITrackedHandle;
    friend class enable_trampoline;

    template <typename>
    friend class Global;
    template <typename>
    friend class Weak;
    template <typename>
    friend class Local;

    std::shared_ptr<void> userData_{nullptr};

    bool isDestroying_{false};

    ITrackedHandle* trackedHead_{nullptr};


    std::unordered_map<std::string, ClassMeta const*> registeredClasses_;

    std::unordered_map<std::string, EnumMeta const*> registeredEnums_;

    std::unordered_map<std::type_index, ClassMeta const*> instanceClassMapping;
};


} // namespace jspp