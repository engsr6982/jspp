#include "QjsEngine.h"

#include "QjsHelper.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Fwd.h"
#include "jspp/core/InstancePayload.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/NativeInstance.h"
#include "jspp/core/Reference.h"
#include "jspp/core/TrackedHandle.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"
#include "queue/JobQueue.h"

#include "quickjs.h"


#include <array>
#include <cassert>
#include <concepts>
#include <type_traits>
#include <utility>


namespace jspp {

namespace qjs_backend {

static_assert(std::is_base_of_v<QjsEngine, Engine>, "Engine must be derived from QjsEngine");
static_assert(std::is_final_v<Engine>, "Engine must be final");

Engine*       QjsEngine::asEngine() { return static_cast<Engine*>(this); }
Engine const* QjsEngine::asEngine() const { return static_cast<Engine const*>(this); }

QjsEngine::QjsEngine() {
    runtime_ = JS_NewRuntime();
    if (!runtime_) {
        throw std::runtime_error("Failed to create JS runtime");
    }
    context_ = JS_NewContext(runtime_);
    if (!context_) {
        throw std::runtime_error("Failed to create JS context");
    }

    queue_ = std::make_unique<JobQueue>();

    {
        JSClassDef pointerClass{};
        pointerClass.class_name = "PointerData";
        JS_NewClassID(runtime_, &pointerDataClassId_);
        JS_NewClass(runtime_, pointerDataClassId_, &pointerClass);

        // for Function::newFunction()
        JSClassDef functionClass{};
        functionClass.class_name = "FunctionData";
        functionClass.finalizer  = [](JSRuntime*, JSValueConst value) {
            auto id      = JS_GetClassID(value);
            auto pointer = JS_GetOpaque(value, id);
            if (pointer) {
                delete static_cast<FunctionCallback*>(pointer);
            }
        };
        JS_NewClassID(runtime_, &functionDataClassId_);
        JS_NewClass(runtime_, functionDataClassId_, &functionClass);
    }

    {
        EngineScope lock{asEngine()};

        auto nativeTag     = Symbol::newSymbol();
        nativeFunctionTag_ = JS_ValueToAtom(context_, QjsHelper::peekValue(nativeTag));

        auto toStringTag = asEngine()->evalScript(String::newString("(Symbol.toStringTag)"));
        if (toStringTag.isSymbol()) {
            toStringTagSymbolAtom_ = JS_ValueToAtom(context_, QjsHelper::peekValue(toStringTag));
        }
    }

    auto base = asEngine();
    JS_SetRuntimeOpaque(runtime_, base);
    JS_SetContextOpaque(context_, base);

    // TODO: init JS_SetModuleLoaderFunc

#ifdef JSPP_DEBUG
    JS_SetDumpFlags(runtime_, JS_DUMP_LEAKS | JS_DUMP_ATOM_LEAKS);
#endif
}

QjsEngine::~QjsEngine() { dispose(); }

void QjsEngine::dispose() {
    if (!context_) return;
    queue_.reset();
    {
        EngineScope scope(asEngine());

        for (auto& [meta, pair] : classConstructors_) {
            JS_FreeValue(context_, pair.first);
            JS_FreeValue(context_, pair.second);
        }
        classConstructors_.clear();

        JS_FreeAtom(context_, nativeFunctionTag_);
        JS_FreeAtom(context_, toStringTagSymbolAtom_);
        // JS_ResetUncatchableError(context_);
        JS_RunGC(runtime_);
    }
    JS_FreeContext(context_);
    context_ = nullptr;
    JS_FreeRuntime(runtime_);
    runtime_ = nullptr;
}

void QjsEngine::pumpPendingJobs() {
    if (asEngine()->isDestroying()) {
        return;
    }
    bool exc = false;
    if (JS_IsJobPending(runtime_) && pumpScheduled_.compare_exchange_strong(exc, true)) {
        queue_->postTask(
            [](void* data) {
                auto        engine = static_cast<Engine*>(data);
                JSContext*  ctx    = nullptr;
                EngineScope lock(engine);
                while (JS_ExecutePendingJob(engine->runtime_, &ctx) > 0) {}
                engine->pumpScheduled_ = false;
            },
            this
        );
    }
}

Local<Value> QjsEngine::loadByteCode(std::filesystem::path const& path, bool main) {
    // TODO: implement this
    return {};
}

PauseGcGuard::PauseGcGuard(Engine* engine) : engine_(engine) { engine_->pauseGcCount_++; }
PauseGcGuard::~PauseGcGuard() { engine_->pauseGcCount_--; }

} // namespace qjs_backend


// impl Engine
Local<Value> Engine::evalScript(Local<String> const& code, Local<String> const& source) {
    auto stdCode   = code.getValue();
    auto stdSource = source.getValue();
    auto result =
        JS_Eval(context_, stdCode.data(), stdCode.size(), stdSource.data(), JS_EVAL_FLAG_STRICT | JS_EVAL_TYPE_GLOBAL);
    qjs_backend::QjsHelper::rethrowException(result);
    pumpPendingJobs();
    return ValueHelper::wrap<Value>(result);
}

void Engine::gc() {
    auto engine = asEngine();

    EngineScope lock{engine};
    if (!engine->isDestroying() && pauseGcCount_ == 0) {
        JS_RunGC(engine->runtime_);
    }
}

Local<Object> Engine::globalThis() const {
    auto global = JS_GetGlobalObject(context_);
    qjs_backend::QjsHelper::rethrowException(global);
    return ValueHelper::wrap<Object>(global);
}

void qjs_backend::QjsEngine::NativeClassFinalizer(JSRuntime*, JSValueConst value) {
    auto id = JS_GetClassID(value);
    assert(id != JS_INVALID_CLASS_ID);
    if (auto opaque = JS_GetOpaque(value, id)) {
        auto payload = static_cast<InstancePayload*>(opaque);
        auto engine  = payload->getEngine();
#ifdef JSPP_DEBUG
        auto iter = engine->classIds_.find(payload->getDefine());
        assert(iter != engine->classIds_.end());
        assert(id == iter->second); // check classId
#endif
        PauseGcGuard guard(engine);
        EngineScope  lock{engine};
        delete payload; // free instance
    }
}

void qjs_backend::QjsEngine::NativeClassGcMarker(JSRuntime* runtime, JSValueConst value, JS_MarkFunc* markFunc) {
    // TODO: implement this
}

Local<Value> Engine::registerClass(ClassMeta const& meta) {
    auto engine = asEngine();
    if (engine->registeredClasses_.contains(meta.name_)) {
        throw std::logic_error("Class already registered: " + meta.name_);
    }

    bool const hasCtor = meta.hasConstructor();
    if (!hasCtor) {
        // static class
        auto object = Object::newObject();
        buildStaticMembers(object, meta);
        setToStringTag(object, meta.name_);
        globalThis().set(String::newString(meta.name_), object);
        engine->registeredClasses_.emplace(meta.name_, &meta);
        return object;
    }

    JSClassDef classDef{};
    classDef.class_name = meta.name_.c_str();
    classDef.finalizer  = NativeClassFinalizer;
    classDef.gc_mark    = NativeClassGcMarker;

    JSClassID id{JS_INVALID_CLASS_ID};
    {
        // alloc classId
        assert(!classIds_.contains(&meta));
        JS_NewClassID(engine->runtime_, &id);
        classIds_.emplace(&meta, id);
    }
    JS_NewClass(runtime_, id, &classDef);

    auto ctor      = newConstructor(meta);
    auto prototype = newClassPrototype(meta);

    auto ctorAsObj = ValueHelper::wrap<Object>(qjs_backend::QjsHelper::getDupLocal(ctor, context_));

    buildStaticMembers(ctorAsObj, meta);

    setToStringTag(prototype, meta.name_); // log(new Foo()) -> [object Foo]
    setToStringTag(ctorAsObj, meta.name_); // log(Foo) -> [object Foo]

    JS_SetConstructor(context_, qjs_backend::QjsHelper::peekValue(ctor), qjs_backend::QjsHelper::peekValue(prototype));
    JS_SetClassProto(context_, id, qjs_backend::QjsHelper::getDupLocal(prototype, context_));

    if (meta.base_ != nullptr) {
        if (meta.base_ == &meta || meta.typeId_ == meta.base_->typeId_) {
            throw std::logic_error("Self-inheritance or same-type inheritance is logically invalid.");
        }
        if (!meta.base_->hasConstructor()) {
            throw Exception("Base class must have a constructor: " + meta.name_);
        }
        auto iter = classConstructors_.find(meta.base_);
        if (iter == classConstructors_.end()) {
            throw Exception("Base class not registered: " + meta.name_);
        }
        // Child.prototype.__proto__ = Parent.prototype;
        JSClassID baseId    = classIds_.at(meta.base_);
        auto      baseProto = JS_GetClassProto(context_, baseId);
        auto      code      = JS_SetPrototype(context_, qjs_backend::QjsHelper::peekValue(prototype), baseProto);
        JS_FreeValue(context_, baseProto);
        qjs_backend::QjsHelper::rethrowException(code);
        // Child.__proto__ = Parent;
        JS_SetPrototype(context_, qjs_backend::QjsHelper::peekValue(ctor), iter->second.first);
    }

    if (meta.hasConstructor()) {
        if (engine->instanceClassMapping.contains(meta.typeId_)) {
            throw std::logic_error("Type already registered: " + meta.name_);
        }
        engine->instanceClassMapping.emplace(meta.typeId_, &meta);
    }
    engine->registeredClasses_.emplace(meta.name_, &meta);

    classConstructors_.emplace(
        &meta,
        std::pair{
            qjs_backend::QjsHelper::getDupLocal(ctor, context_),
            qjs_backend::QjsHelper::getDupLocal(prototype, context_)
        }
    );

    globalThis().set(String::newString(meta.name_), ctor);
    return ctor;
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
    if (!meta.hasConstructor()) {
        return false; // target is a static class
    }
    auto iter = classConstructors_.find(&meta);
    if (iter == classConstructors_.end()) {
        return false; // meta not registered
    }
    auto code = JS_IsInstanceOf(context_, qjs_backend::QjsHelper::peekValue(obj), iter->second.first);
    qjs_backend::QjsHelper::rethrowException(code);
    return code == 1;
}

Local<Object> Engine::newInstance(ClassMeta const& meta, std::unique_ptr<NativeInstance>&& instance) {
    auto iter = classConstructors_.find(&meta);
    if (iter == classConstructors_.end()) {
        throw std::logic_error("Class not registered: " + meta.name_);
    }

    auto external = JS_NewObjectClass(context_, pointerDataClassId_);
    qjs_backend::QjsHelper::rethrowException(external);

    JS_SetOpaque(external, instance.release());

    std::array<JSValue, 1> args = {external};

    auto ctor   = iter->second.first;
    auto result = JS_CallConstructor(context_, ctor, args.size(), args.data());
    if (JS_IsException(result)) {
        // If the constructor throws an exception, the opaque pointer will be leak
        auto leakInst = JS_GetOpaque(external, pointerDataClassId_);
        instance.reset(static_cast<NativeInstance*>(leakInst)); // restore instance
    }
    JS_FreeValue(context_, external);
    qjs_backend::QjsHelper::rethrowException(result);
    pumpPendingJobs();
    return ValueHelper::wrap<Object>(result);
}

InstancePayload* Engine::getInstancePayload(Local<Object> const& obj) const {
    auto val = qjs_backend::QjsHelper::peekValue(obj);
    auto id  = JS_GetClassID(val);
    // TODO: ensure id is registered class
    if (auto opaque = JS_GetOpaque(qjs_backend::QjsHelper::peekValue(obj), id)) {
        return static_cast<InstancePayload*>(opaque);
    }
    return nullptr;
}

bool Engine::trySetReferenceInternal(Local<Object> const& parentObj, Local<Object> const& subObj) {
    // TODO: please implement this
    // Implement internal references; this function affects the stability of the trampolining mechanism.
    // For the v8 backend, parentObj is usually placed into an internal field of subObj.
    return false;
}

namespace qjs_backend {

void QjsEngine::setToStringTag(Local<Object>& obj, std::string_view name) {
    JS_DefinePropertyValue(
        context_,
        QjsHelper::peekValue(obj),
        toStringTagSymbolAtom_,
        JS_NewStringLen(context_, name.data(), name.size()),
        JS_PROP_CONFIGURABLE // only allow configurables
    );
}

Local<Function> QjsEngine::newDataFunction(DataFunctionCallback callback, void* data1, void* data2) {
    static auto newPointerData = [](Engine* engine, void* ptr) {
        if (!ptr) {
            return JS_NULL;
        }
        auto data = JS_NewObjectClass(engine->context_, engine->pointerDataClassId_);
        QjsHelper::rethrowException(data);
        JS_SetOpaque(data, ptr);
        return data;
    };

    auto engine   = asEngine();
    auto opaque1  = newPointerData(engine, data1);
    auto opaque2  = newPointerData(engine, data2);
    auto opaqueCb = newPointerData(engine, reinterpret_cast<void*>(callback));

    auto args = std::array<JSValue, 3>{opaque1, opaque2, opaqueCb};

    auto fn = JS_NewCFunctionData(
        context_,
        [](JSContext* ctx, JSValueConst thiz, int argc, JSValueConst* argv, int /* magic */, JSValueConst* data)
            -> JSValue {
            auto engine = static_cast<Engine*>(JS_GetContextOpaque(ctx));

            auto data1    = JS_GetOpaque(data[0], engine->pointerDataClassId_);
            auto data2    = JS_GetOpaque(data[1], engine->pointerDataClassId_);
            auto callback = reinterpret_cast<DataFunctionCallback>(JS_GetOpaque(data[2], engine->pointerDataClassId_));

            try {
                auto args = QjsHelper::wrapArguments(engine, thiz, argc, argv);
                auto ret  = callback(args, data1, data2);
                return QjsHelper::getDupLocal(ret, engine->context_);
            } catch (Exception const& e) {
                return QjsHelper::rethrowToScript(e);
            }
        },
        0,
        0,
        args.size(),
        args.data()
    );

    JS_FreeValue(context_, opaque1);
    JS_FreeValue(context_, opaque2);
    JS_FreeValue(context_, opaqueCb);

    QjsHelper::rethrowException(fn);
    return ValueHelper::wrap<Function>(fn);
}

Local<Function> QjsEngine::newConstructor(ClassMeta const& meta) {
    auto ctor = newDataFunction(
        [](Arguments const& args, void* data1, void* /* data2 */) -> Local<Value> {
            auto meta = static_cast<ClassMeta*>(data1);

            auto engine = args.runtime();

            auto scriptThis = QjsHelper::peekValue(args.thiz());
            if (!JS_IsConstructor(engine->context_, scriptThis)) [[unlikely]] {
                throw Exception("Constructor called as a function");
            }

            Local<Value> proto;
            {
                auto prototype = JS_GetPropertyStr(engine->context_, scriptThis, "prototype");
                QjsHelper::rethrowException(prototype);

                auto rawProto = JS_NewObjectProtoClass(engine->context_, prototype, engine->classIds_.at(meta));
                JS_FreeValue(engine->context_, prototype);
                QjsHelper::rethrowException(rawProto);

                proto = ValueHelper::wrap<Value>(rawProto); // RAII management
            }

            std::unique_ptr<NativeInstance> instance        = nullptr;
            bool                            constructFromJs = false;
            if (args.length() == 1) {
                auto maybeInstance = QjsHelper::peekValue(args[0]);
                auto id            = JS_GetClassID(maybeInstance);
                auto opaque        = JS_GetOpaque(maybeInstance, id);
                if (id != JS_INVALID_CLASS_ID && id == engine->pointerDataClassId_ && opaque) {
                    instance        = std::unique_ptr<NativeInstance>{static_cast<NativeInstance*>(opaque)};
                    constructFromJs = false; // c++ construct
                }
            }

            if (!instance) {
                constructFromJs     = true;
                auto& unConst       = const_cast<Arguments&>(args);
                unConst.impl_.thiz_ = QjsHelper::peekValue(proto);
                instance            = (meta->instanceMeta_.constructor_)(args);
            }

            if (!instance) [[unlikely]] {
                throw Exception{"This native class cannot be constructed."};
            }

            auto payload = new InstancePayload{std::move(instance), meta, engine, constructFromJs};
            JS_SetOpaque(QjsHelper::peekValue(proto), payload);

            engine->initClassTrampoline(payload, proto.asObject());

            return proto;
        },
        const_cast<ClassMeta*>(&meta),
        nullptr
    );
    auto code = JS_SetConstructorBit(context_, QjsHelper::peekValue(ctor), true);
    QjsHelper::rethrowException(code);
    return ctor;
}

void QjsEngine::buildStaticMembers(Local<Object>& obj, ClassMeta const& meta) {
    auto const& staticMeta = meta.staticMeta_;

    for (auto& fnMeta : staticMeta.functions_) {
        auto fn = newDataFunction(
            [](Arguments const& args, void* data1, void* /* data2 */) -> Local<Value> {
                auto func = static_cast<StaticMemberMeta::Function*>(data1);
                return func->callback_(args);
            },
            const_cast<StaticMemberMeta::Function*>(&fnMeta),
            nullptr
        );
        bool ok = obj.defineOwnProperty(String::newString(fnMeta.name_), fn, PropertyAttribute::DontDelete);
        assert(ok);
        (void)ok; // unused
    }

    for (auto& propMeta : staticMeta.property_) {
        Local<Value> getter;
        Local<Value> setter;

        getter = newDataFunction(
            [](Arguments const& /* args */, void* data1, void* /* data2 */) -> Local<Value> {
                auto property = static_cast<StaticMemberMeta::Property*>(data1);
                return property->getter_();
            },
            const_cast<StaticMemberMeta::Property*>(&propMeta),
            nullptr
        );

        if (propMeta.setter_) {
            setter = newDataFunction(
                [](Arguments const& args, void* data1, void* /* data2 */) -> Local<Value> {
                    auto property = static_cast<StaticMemberMeta::Property*>(data1);
                    property->setter_(args[0]);
                    return {};
                },
                const_cast<StaticMemberMeta::Property*>(&propMeta),
                nullptr
            );
        }

        auto atom = JS_NewAtomLen(context_, propMeta.name_.data(), propMeta.name_.size());
        auto code = JS_DefinePropertyGetSet(
            context_,
            QjsHelper::peekValue(obj),
            atom,
            QjsHelper::getDupLocal(getter),
            QjsHelper::getDupLocal(setter),
            QjsHelper::castAttribute(PropertyAttribute::DontDelete)
        );
        JS_FreeAtom(context_, atom);
        QjsHelper::rethrowException(code);
    }
}

bool ensurePrototypeSignature(ClassMeta const* meta, ClassMeta const* target) {
    // TODO(optimization): For each ClassMeta, maintain a cached unordered_set of all ancestor classes.
    // Then implement isFamily(target) to quickly check if a class is derived from target.
    // This avoids repeatedly walking the inheritance chain in high-frequency calls.
    while (meta) {
        if (meta == target) {
            return true;
        }
        meta = target->base_;
    }
    return false;
}

Local<Object> QjsEngine::newClassPrototype(ClassMeta const& meta) {
    auto& instanceMeta = meta.instanceMeta_;

    auto prototype = Object::newObject();

    for (auto& fnMeta : instanceMeta.methods_) {
        auto fn = newDataFunction(
            [](Arguments const& args, void* data1, void* data2) -> Local<Value> {
                auto func = static_cast<InstanceMemberMeta::Method*>(data1);
                auto meta = static_cast<ClassMeta*>(data2);

                auto id = JS_GetClassID(args.impl_.thiz_);
                assert(id != JS_INVALID_CLASS_ID);

                auto payload = static_cast<InstancePayload*>(JS_GetOpaque(args.impl_.thiz_, id));
                if (!payload) [[unlikely]] {
                    throw Exception{"Method called on incompatible receiver", ExceptionType::TypeError};
                }

                if (!ensurePrototypeSignature(payload->getDefine(), meta)) [[unlikely]] {
                    throw Exception{"Class signature mismatch", ExceptionType::TypeError};
                }

                return func->callback_(*payload, args);
            },
            const_cast<InstanceMemberMeta::Method*>(&fnMeta),
            const_cast<ClassMeta*>(&meta)
        );
        // set nativeFunctionTag_ for trampoline function
        JS_SetProperty(context_, QjsHelper::peekValue(fn), nativeFunctionTag_, JS_NewBool(context_, true));
        bool ok = prototype.defineOwnProperty(String::newString(fnMeta.name_), fn, PropertyAttribute::DontDelete);
        assert(ok);
        (void)ok; // Suppress unused variable warning
    }

    for (auto& propMeta : instanceMeta.property_) {
        Local<Value> getter;
        Local<Value> setter;

        getter = newDataFunction(
            [](Arguments const& args, void* data1, void* data2) -> Local<Value> {
                auto prop = static_cast<InstanceMemberMeta::Property*>(data1);
                auto meta = static_cast<ClassMeta*>(data2);

                auto id = JS_GetClassID(args.impl_.thiz_);
                assert(id != JS_INVALID_CLASS_ID);

                auto payload = static_cast<InstancePayload*>(JS_GetOpaque(args.impl_.thiz_, id));
                if (!payload) [[unlikely]] {
                    throw Exception{"Getter called on incompatible receiver", ExceptionType::TypeError};
                }

                if (!ensurePrototypeSignature(payload->getDefine(), meta)) [[unlikely]] {
                    throw Exception{"Class signature mismatch", ExceptionType::TypeError};
                }

                return prop->getter_(*payload, args);
            },
            const_cast<InstanceMemberMeta::Property*>(&propMeta),
            const_cast<ClassMeta*>(&meta)
        );
        if (propMeta.setter_) {
            setter = newDataFunction(
                [](Arguments const& args, void* data1, void* data2) -> Local<Value> {
                    auto prop = static_cast<InstanceMemberMeta::Property*>(data1);
                    auto meta = static_cast<ClassMeta*>(data2);

                    auto id = JS_GetClassID(args.impl_.thiz_);
                    assert(id != JS_INVALID_CLASS_ID);

                    auto payload = static_cast<InstancePayload*>(JS_GetOpaque(args.impl_.thiz_, id));
                    if (!payload) [[unlikely]] {
                        throw Exception{"Setter called on incompatible receiver", ExceptionType::TypeError};
                    }

                    if (!ensurePrototypeSignature(payload->getDefine(), meta)) [[unlikely]] {
                        throw Exception{"Class signature mismatch", ExceptionType::TypeError};
                    }

                    prop->setter_(*payload, args);
                    return {};
                },
                const_cast<InstanceMemberMeta::Property*>(&propMeta),
                const_cast<ClassMeta*>(&meta)
            );
        }

        auto atom = JS_NewAtomLen(context_, propMeta.name_.data(), propMeta.name_.size());
        auto ret  = JS_DefinePropertyGetSet(
            context_,
            QjsHelper::peekValue(prototype),
            atom,
            QjsHelper::getDupLocal(getter),
            QjsHelper::getDupLocal(setter),
            QjsHelper::castAttribute(PropertyAttribute::DontDelete)
        );
        JS_FreeAtom(context_, atom);
        QjsHelper::rethrowException(ret);
    }

    return prototype;
}

} // namespace qjs_backend

} // namespace jspp