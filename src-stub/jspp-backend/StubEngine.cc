#include "StubEngine.h"

#include "StubHelper.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/InstancePayload.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/Reference.h"
#include "jspp/core/TrackedHandle.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

#include <cassert>
#include <concepts>
#include <type_traits>


namespace jspp {

namespace stub_backend {

static_assert(std::is_base_of_v<StubEngine, Engine>, "Engine must be derived from StubEngine");
static_assert(std::is_final_v<Engine>, "Engine must be final");

Engine*       StubEngine::asEngine() { return static_cast<Engine*>(this); }
Engine const* StubEngine::asEngine() const { return static_cast<Engine const*>(this); }

StubEngine::StubEngine() {
    // TODO: please init this properly

    // constructorSymbol_.reset(Symbol::newSymbol("constructorSymbol_"));
    // nativeFunctionTag_.reset(Symbol::newSymbol("nativeFunctionTag"));
}

StubEngine::~StubEngine() {
    {
        EngineScope scope(asEngine());

        // TODO: free all resources
        // constructorSymbol_.reset();
        // nativeFunctionTag_.reset();
    }
}

} // namespace stub_backend


// impl Engine
Local<Value> Engine::evalScript(Local<String> const& code, Local<String> const& source) {
    // TODO: please implement this
    return {};
}

void Engine::gc() {
    // TODO: please implement this
}

Local<Object> Engine::globalThis() const {
    // TODO: please implement this
    throw Exception("Not implemented");
}

Local<Value> Engine::registerClass(ClassMeta const& meta) {
    auto engine = asEngine();
    if (engine->registeredClasses_.contains(meta.name_)) {
        throw std::logic_error("Class already registered: " + meta.name_);
    }

    // TODO: please implement this
    if (meta.hasConstructor()) {
        // Call newConstructor(meta) to create an instance constructor
    } else {
        // Static class, mounted as a regular object
        // If possible, please remove the prototype chain
    }

    // If possible, please set the class name and its toStringTag, for example:
    // setToStringTag(ctor, meta.name_, meta.hasConstructor());
    // Or an interface like V8's SetClassName

    // If there is no other logic, modify the template code here to assemble the class.
    // buildStaticMembers(ctor, meta);
    // buildInstanceMembers(ctor, meta);

    if (meta.base_ != nullptr) {
        if (meta.base_ == &meta || meta.typeId_ == meta.base_->typeId_) {
            throw std::logic_error("Self-inheritance or same-type inheritance is logically invalid.");
        }
        if (!meta.base_->hasConstructor()) {
            throw Exception("Base class must have a constructor: " + meta.name_);
        }
        // TODO: Please implement class inheritance here (be sure to check whether the base class is registered)
    }

    // If there is an exception, please check

    if (meta.hasConstructor()) {
        if (engine->instanceClassMapping.contains(meta.typeId_)) {
            throw std::logic_error("Type already registered: " + meta.name_);
        }
        engine->instanceClassMapping.emplace(meta.typeId_, &meta);
    }
    engine->registeredClasses_.emplace(meta.name_, &meta);
    // Please cache the class constructor here

    // Return the constructor after attaching the class to globalThis
    throw Exception("Not implemented");
}

Local<Object> Engine::registerEnum(EnumMeta const& meta) {
    if (registeredEnums_.contains(meta.name_)) {
        throw std::logic_error("Enum already registered: " + meta.name_);
    }

    auto object = Object::newObject();
    for (auto const& [name, value] : meta.entries_) {
        object.set(String::newString(name), Number::newNumber(static_cast<double>(value)));
    }

    (void)object.defineOwnProperty(
        String::newString("$name"),
        String::newString(meta.name_),
        PropertyAttribute::DontEnum | PropertyAttribute::DontDelete | PropertyAttribute::ReadOnly
    );

    setToStringTag(object, meta.name_);

    registeredEnums_.emplace(meta.name_, &meta);

    globalThis().set(String::newString(meta.name_), object);
    return object;
}

bool Engine::isInstanceOf(Local<Object> const& obj, ClassMeta const& meta) const {
    // TODO: please implement this
    // Perform a strict comparison using the constructor cached in the previous step
    return false;
}

Local<Object> Engine::newInstance(ClassMeta const& meta, std::unique_ptr<NativeInstance>&& instance) {
    // TODO: please implement this
    // Create an instance by calling the constructor cached in the previous step
    // For safety, a unique flag needs to be specified when calling, for example
    //  arg1: constructorSymbol_, arg2: instance
    throw Exception("Not implemented");
}

InstancePayload* Engine::getInstancePayload(Local<Object> const& obj) const {
    // TODO: please implement this
    return nullptr;
}

bool Engine::trySetReferenceInternal(Local<Object> const& parentObj, Local<Object> const& subObj) {
    // TODO: please implement this
    // Implement internal references; this function affects the stability of the trampolining mechanism.
    // For the v8 backend, parentObj is usually placed into an internal field of subObj.
    return false;
}

namespace stub_backend {

void StubEngine::setToStringTag(Local<Object>& obj, std::string_view name, bool hasConstructor) {
    // TODO: please implement this
    // This function affects the behavior of Native Class's toString in JS.
    // If implemented incorrectly or not implemented,
    //  it will cause the script toString to return the default [object Object], for example:
    //  log(new Foo) -> [object Object]
    //  log(Bar) -> [object Object]
    // Note: toStringTag needs to be set as non-enumerable and non-deletable
}

void StubEngine::setToStringTag(Local<Object>& obj, std::string_view name) { setToStringTag(obj, name, false); }

Local<Function> StubEngine::newConstructor(ClassMeta const& meta) {
    // TODO: please implement this

    // In the construction callback, the following logic needs to be processed in order;
    // otherwise, it may cause abnormal behavior.

    // 1. Use try to catch possible Exceptions
    // 2. Check whether the constructor is called as a function
    // 3. Handle C actively constructing an instance (call newInstance)
    // 4. Check whether the instance is successfully constructed, and throw an exception if it fails
    // 5. Store the instance into InstancePayload and set it to the script instance
    // 6. Optional, for some engines it may be necessary to set external memory, such as V8
    // 7. Call initClassTrampoline to initialize the trampoline class, a must!

    throw Exception("Not implemented");
}

void StubEngine::buildStaticMembers(Local<Object>& obj, ClassMeta const& meta) {
    auto const& staticMeta = meta.staticMeta_;

    // TODO: please implement this
    // Please implement the logic of static members, including static properties and static methods
    // Pay attention to static properties and static methods, both need to be set to non-deletable.
    // Static methods need to prohibit constructing calls from scripts.
}
void StubEngine::buildInstanceMembers(Local<Object>& obj, ClassMeta const& meta) {
    auto& instanceMeta = meta.instanceMeta_;

    // TODO: please implement this
    // Please implement the logic of instance members, including instance properties and instance methods

    // Warning:
    //  For instance classes, a signature check is required for each call,
    //   otherwise it may cause the host to crash
    //  For example: A.prototype.foo = B.prototype.bar; once the script calls foo,
    //   it will directly trigger a segmentation fault.
    //  For this issue, in v8 the problem can be solved using the Signature mechanism,
    //   while other engines require similar security checks.

    // For instance methods, each function needs to set nativeFunctionTag_.
    // If it is not set, when the script calls the trampoline class super.bar(),
    //  the trampoline class cannot distinguish whether the function is overridden,
    //  which will cause the function to call itself recursively, eventually leading to a stack overflow crash.

    // Similarly, the properties and methods of the instance class need to be set to prohibit deletion.
}

} // namespace stub_backend

} // namespace jspp