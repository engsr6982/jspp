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
#include "jspp/core/Trampoline.h"
#include "jspp/core/Utils.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"
#include "queue/JobQueue.h"

#include <array>
#include <cassert>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <istream>
#include <ranges>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <cstring>

JSPP_WARNING_GUARD_BEGIN
#include "quickjs.h"
JSPP_WARNING_GUARD_END

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <dlfcn.h>
#endif

#ifndef QJS_NATIVE_MODULE_SUFFIX
#ifdef _WIN32
#define QJS_NATIVE_MODULE_SUFFIX ".dll"
#else
#define QJS_NATIVE_MODULE_SUFFIX ".so"
#endif
#endif

namespace {

#ifdef _WIN32
[[nodiscard]] void* load_dyn_lib(const wchar_t* path) { return LoadLibraryW(path); }
#else
[[nodiscard]] void* load_dyn_lib(const char* path) { return dlopen(path, RTLD_LAZY | RTLD_LOCAL); }
#endif

void free_dyn_lib(void* h) {
#ifdef _WIN32
    FreeLibrary(static_cast<HMODULE>(h));
#else
    dlclose(h);
#endif
}

void* get_dyn_sym(void* h, const char* name) {
#ifdef _WIN32
    return GetProcAddress(static_cast<HMODULE>(h), name);
#else
    return dlsym(h, name);
#endif
}

} // namespace

namespace jspp {
namespace qjs_backend {
static_assert(std::is_base_of_v<QjsEngine, Engine>, "Engine must be derived from QjsEngine");
static_assert(std::is_final_v<Engine>, "Engine must be final");

Engine*       QjsEngine::asEngine() { return static_cast<Engine*>(this); }
Engine const* QjsEngine::asEngine() const { return static_cast<Engine const*>(this); }

void QjsEngine::initContext() {
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

        auto nativeTag     = Symbol::newSymbol("nativeFunctionTag_");
        nativeFunctionTag_ = JS_ValueToAtom(context_, QjsHelper::peekValue(nativeTag));

        auto toStringTag = asEngine()->evalScript(String::newString("(Symbol.toStringTag)"));
        if (toStringTag.isSymbol()) {
            toStringTagSymbolAtom_ = JS_ValueToAtom(context_, QjsHelper::peekValue(toStringTag));
        }

        JSValue refSym         = JS_NewSymbol(context_, "reference_internal", false);
        referenceInternalAtom_ = JS_ValueToAtom(context_, refSym);
        JS_FreeValue(context_, refSym);
    }

    if (!hasFlag(flags_, QjsInitializeFlags::NoModuleLoader)) {
        JS_SetModuleLoaderFunc2(
            runtime_,
            QjsModuleLoader::normalize,
            QjsModuleLoader::loader,
            QjsModuleLoader::attrChecker,
            asEngine()
        );
    }

#ifdef JSPP_DEBUG
    JS_SetDumpFlags(runtime_, JS_DUMP_LEAKS | JS_DUMP_ATOM_LEAKS);
#endif
}

QjsEngine::QjsEngine(JSRuntimeFactory const& factory, QjsInitializeFlags flags) : flags_(flags) {
    runtime_ = factory ? factory() : JS_NewRuntime();
    if (!runtime_) {
        throw std::runtime_error("Failed to create JS runtime");
    }
    context_ = JS_NewContext(runtime_);
    if (!context_) {
        throw std::runtime_error("Failed to create JS context");
    }
    initContext();
}

QjsEngine::QjsEngine(JSRuntime* runtime, JSContext* context, QjsInitializeFlags flags)
: runtime_(runtime),
  context_(context),
  flags_(flags) {
    initContext();
}

QjsEngine::~QjsEngine() { dispose(); }

void QjsEngine::dispose() {
    if (!context_) return;
    queue_.reset();
    {
        EngineScope scope(asEngine());

        for (auto& pair : classConstructors_ | std::views::values) {
            JS_FreeValue(context_, pair.first);
            JS_FreeValue(context_, pair.second);
        }
        for (auto& v : staticClassObject_ | std::views::values) {
            JS_FreeValue(context_, v);
        }
        for (auto& v : enumObject_ | std::views::values) {
            v.reset();
        }

        classConstructors_.clear();
        staticClassObject_.clear();
        enumObject_.clear();
        module2meta_.clear();

        JS_FreeAtom(context_, nativeFunctionTag_);
        JS_FreeAtom(context_, toStringTagSymbolAtom_);
        JS_FreeAtom(context_, referenceInternalAtom_);

        if (!hasFlag(flags_, QjsInitializeFlags::NoGcInDispose)) {
            JS_RunGC(runtime_);
        }
    }
    if (!hasFlag(flags_, QjsInitializeFlags::NoFreeContext)) {
        JS_FreeContext(context_);
    }
    if (!hasFlag(flags_, QjsInitializeFlags::NoFreeRuntime)) {
        JS_FreeRuntime(runtime_);
    }
    context_ = nullptr;
    runtime_ = nullptr;
}
JSRuntime* QjsEngine::runtime() const { return runtime_; }
JSContext* QjsEngine::context() const { return context_; }

void QjsEngine::pumpPendingJobs() {
    if (hasFlag(flags_, QjsInitializeFlags::NoJobQueuePosting)) {
        return;
    }
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

JobQueue* QjsEngine::getJobQueue() const { return queue_.get(); }

Local<Value> QjsEngine::loadByteCode(std::filesystem::path const& path, bool main) {
    std::ifstream ifs{path, std::ios::binary};
    if (!ifs) {
        throw std::runtime_error("Failed to open binary file: " + path.string());
    }
    auto buffer = std::vector<uint8_t>((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    ifs.close();

    JSValue obj = JS_ReadObject(context_, buffer.data(), buffer.size(), JS_READ_OBJ_BYTECODE);
    QjsHelper::rethrowException(obj);

    if (JS_VALUE_GET_TAG(obj) == JS_TAG_MODULE) {
        if (JS_ResolveModule(context_, obj) < 0) {
            JS_FreeValue(context_, obj);
            QjsHelper::rethrowException(-1, "Failed to resolve module");
        }
        auto url = path.is_absolute() ? path.string() : std::filesystem::absolute(path).string();
#ifdef _WIN32
        std::replace(url.begin(), url.end(), '\\', '/');
#endif
        if (!url.starts_with(QjsModuleLoader::kFileUrlPrefix)) {
            url = std::string{QjsModuleLoader::kFileUrlPrefix} + url;
        }
        auto module = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(obj));
        if (!QjsModuleLoader::setImportMeta(context_, module, url, main)) {
            JS_FreeValue(context_, obj);
            QjsHelper::rethrowException(-1, "Failed to set import meta");
        }
    }

    obj = JS_EvalFunction(context_, obj);
    QjsHelper::rethrowException(obj);

    // check promise rejected
    JSPromiseStateEnum state = JS_PromiseState(context_, obj);
    if (state == JSPromiseStateEnum::JS_PROMISE_REJECTED) {
        JSValue msg = JS_PromiseResult(context_, obj);
        JS_FreeValue(context_, obj);                  // Release the execution result
        JS_Throw(context_, msg);                      // Throw a pending exception
        qjs_backend::QjsHelper::rethrowException(-1); // Handle pending exception
    }

    pumpPendingJobs();
    return ValueHelper::wrap<Value>(obj);
}

bool QjsModuleLoader::setImportMeta(
    JSContext*                 ctx,
    JSModuleDef*               def,
    std::optional<std::string> url,
    std::optional<bool>        main
) {
    if (!url.has_value() && !main.has_value()) {
        return false;
    }

    auto meta = JS_GetImportMeta(ctx, def);
    if (JS_IsException(meta)) {
        return false;
    }

    if (main.has_value()) {
        auto code = JS_DefinePropertyValueStr(ctx, meta, "main", JS_NewBool(ctx, *main), JS_PROP_C_W_E);
        if (code < 0) {
            // error
            JS_FreeValue(ctx, meta);
            return false;
        }
    }

    if (url.has_value()) {
        auto code =
            JS_DefinePropertyValueStr(ctx, meta, "url", JS_NewStringLen(ctx, url->data(), url->size()), JS_PROP_C_W_E);
        if (code < 0) {
            // error
            JS_FreeValue(ctx, meta);
            return false;
        }
    }

    JS_FreeValue(ctx, meta);
    return true;
}
char* QjsModuleLoader::normalize(
    JSContext*  ctx,
    const char* importingModule,
    const char* importSpecifier,
    void*       opaque
) {
    auto engine = static_cast<Engine*>(opaque);

    auto sv_source    = std::string_view{importingModule};
    auto sv_specifier = std::string_view{importSpecifier};

    // native module (e.g. "os", "fs")
    if (engine->getModuleMeta(importSpecifier)) {
        return js_strdup(ctx, importSpecifier);
    }

    // <...>
    if (sv_source.starts_with('<') && sv_source.ends_with('>')) {
        if (!sv_specifier.starts_with("/") && !sv_specifier.starts_with(kFileUrlPrefix)) {
            JS_ThrowPlainError(ctx, "cannot resolve relative import '%s' from non-file source", importSpecifier);
            return nullptr;
        }
    }

    bool is_relative = sv_specifier.starts_with("./") || sv_specifier.starts_with("../");
    bool is_absolute = sv_specifier.starts_with("/");
    bool is_file_url = sv_specifier.starts_with(kFileUrlPrefix);

    if (!is_relative && !is_absolute && !is_file_url) {
        JS_ThrowPlainError(ctx, "extending bare specifier resolution is not supported: %s", importSpecifier);
        return nullptr;
    }
    std::filesystem::path final_path;

    if (is_absolute) {
        final_path = sv_specifier;
    } else if (is_file_url) {
        std::string p{sv_specifier};
        p.erase(0, kFileUrlPrefix.size());
#ifdef _WIN32
        // Windows: file:///C:/path -> C:/path
        if (p.starts_with("/")) p.erase(0, 1);
#endif
        final_path = p;
    } else {
        // resolve relative path
        std::string cp_base{sv_source};
        if (cp_base.starts_with(kFileUrlPrefix)) {
            cp_base.erase(0, kFileUrlPrefix.size());
#ifdef _WIN32
            if (cp_base.starts_with("/")) cp_base.erase(0, 1);
#endif
        }

        std::filesystem::path base_path{cp_base};
        final_path = base_path.parent_path() / sv_specifier;
    }

    std::error_code err;
    final_path = std::filesystem::weakly_canonical(final_path, err);
    if (err) {
        JS_ThrowPlainError(ctx, "resolution failed: %s", importSpecifier);
        return nullptr;
    }

    if (!std::filesystem::is_regular_file(final_path)) {
        JS_ThrowPlainError(ctx, "module not found: %s", final_path.string().c_str());
        return nullptr;
    }

    std::string gen_path  = final_path.generic_string();
    std::string final_url = "file://";
    if (!gen_path.starts_with("/")) {
        final_url += "/"; // file:///C:/...
    }
    final_url += gen_path;

    return js_strdup(ctx, final_url.c_str());
}
JSModuleDef* QjsModuleLoader::loader(JSContext* ctx, const char* specifier, void* opaque, JSValueConst attr) {
    auto engine = static_cast<Engine*>(opaque);

    if (auto meta = engine->getModuleMeta(specifier)) {
        return performNewNativeModule(engine, meta);
    }

    namespace fs = std::filesystem;
    if (std::strncmp(specifier, kFileUrlPrefix.data(), kFileUrlPrefix.size()) == 0) {
        std::string url = specifier + kFileUrlPrefix.size(); // remove "file://"
#ifdef _WIN32
        if (url.starts_with("/")) url.erase(0, 1); // from /C:/... to C:/...
#endif
        fs::path modulePath = url;

        auto type = resolveImportAttr(ctx, attr);
        if (type == ImportAttrType::Unknown) {
            return nullptr; // have already thrown exception
        }

        auto ext = modulePath.extension();
        if (ext == QJS_NATIVE_MODULE_SUFFIX) {
            return performLoadCModule(engine, modulePath, specifier);
        }
        if (ext == ".bin") {
            return performLoadByteCodeModule(engine, modulePath, specifier);
        }
        if (type == ImportAttrType::Json) {
            return performLoadJsonModule(engine, modulePath, specifier);
        }
        return performLoadScriptModule(engine, modulePath, specifier);
    }
    return nullptr;
}
int QjsModuleLoader::attrChecker(JSContext* ctx, void* /*opaque*/, JSValue attr) {
    // copy from js_module_check_attributes
    JSPropertyEnum* tab;
    uint32_t        len;
    if (!JS_GetOwnPropertyNames(ctx, &tab, &len, attr, JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY)) {
        return -1;
    }
    int         ret = 0;
    const char* cstr;
    size_t      cstr_len;
    for (uint32_t i = 0; i < len; i++) {
        cstr = JS_AtomToCStringLen(ctx, &cstr_len, tab[i].atom);
        if (!cstr) {
            ret = -1;
            break;
        }
        if (!(cstr_len == 4 && !memcmp(cstr, "type", cstr_len))) {
            JS_ThrowTypeError(ctx, "import attribute '%s' is not supported", cstr);
            ret = -1;
        }
        JS_FreeCString(ctx, cstr);
        if (ret) break;
    }
    JS_FreePropertyEnum(ctx, tab, len);
    return ret;
}
QjsModuleLoader::ImportAttrType QjsModuleLoader::resolveImportAttr(JSContext* ctx, JSValue attr) {
    if (JS_IsUndefined(attr)) {
        return ImportAttrType::Js; // Attribute not specified, default is js
    }
    JSValue str = JS_GetPropertyStr(ctx, attr, "type");
    if (JS_IsException(str)) {
        return ImportAttrType::Unknown;
    }
    if (!JS_IsString(str)) {
        JS_ThrowTypeError(ctx, "import attribute 'type' must be a string");
        JS_FreeValue(ctx, str);
        return ImportAttrType::Unknown;
    }

    size_t len;
    auto   cstr = JS_ToCStringLen(ctx, &len, str);
    JS_FreeValue(ctx, str);
    if (!cstr) {
        return ImportAttrType::Unknown;
    }

    auto resolved = ImportAttrType::Unknown;
    auto sv_type  = std::string_view{cstr, len};
    if (sv_type == "js") {
        resolved = ImportAttrType::Js;
    } else if (sv_type == "json") {
        resolved = ImportAttrType::Json;
    } else {
        // TODO: Provide extension hooks for other types
        JS_ThrowTypeError(ctx, "unsupported import attribute 'type': '%s'", cstr);
    }
    JS_FreeCString(ctx, cstr);
    return resolved;
}
JSModuleDef*
QjsModuleLoader::performLoadCModule(Engine* engine, std::filesystem::path const& path, const char* specifier) {
    if (!hasFlag(engine->flags_, QjsInitializeFlags::EnableQuickJsCModuleSupport)) {
        JS_ThrowPlainError(engine->context_, "quickjs c module support is disabled");
        return nullptr;
    }
    auto h = load_dyn_lib(path.c_str());
    if (!h) {
        JS_ThrowPlainError(engine->context_, "failed to load c module: %s", path.string().c_str());
        return nullptr;
    }
    auto fn = get_dyn_sym(h, kCModuleEntry);
    if (!fn) {
        JS_ThrowPlainError(engine->context_, "failed to find c module entry: %s", path.string().c_str());
        free_dyn_lib(h);
        return nullptr;
    }
    auto init = reinterpret_cast<QjsCModuleInitFunc>(fn);
    auto def  = init(engine->context_, specifier);
    if (!def) {
        JS_ThrowPlainError(engine->context_, "failed to initialize c module: %s", path.string().c_str());
        free_dyn_lib(h);
        return nullptr;
    }
    return def;
}
JSModuleDef*
QjsModuleLoader::performLoadJsonModule(Engine* engine, std::filesystem::path const& path, const char* specifier) {
    if (path.extension() != ".json") {
        JS_ThrowTypeError(engine->context_, "invalid json module: %s", path.string().c_str());
        return nullptr;
    }
    auto ifs = std::ifstream{path};
    if (!ifs) [[unlikely]] {
        JS_ThrowPlainError(engine->context_, "module not found: %s", path.string().c_str());
        return nullptr;
    }
    auto source = std::string{std::istreambuf_iterator<char>{ifs}, std::istreambuf_iterator<char>{}};
    ifs.close();

    auto v = JS_ParseJSON(engine->context_, source.c_str(), source.size(), specifier);
    if (JS_IsException(v)) {
        return nullptr;
    }
    auto m = JS_NewCModule(engine->context_, specifier, [](JSContext* ctx, JSModuleDef* def) -> int {
        auto json = JS_GetModulePrivateValue(ctx, def);
        return JS_SetModuleExport(ctx, def, kDefaultExport.data(), json);
    });
    if (!m) {
        JS_FreeValue(engine->context_, v);
        return nullptr;
    }
    JS_AddModuleExport(engine->context_, m, kDefaultExport.data());
    JS_SetModulePrivateValue(engine->context_, m, v);
    return m;
}
JSModuleDef*
QjsModuleLoader::performLoadByteCodeModule(Engine* engine, std::filesystem::path const& path, const char* specifier) {
    auto ifs = std::ifstream{path, std::ios::binary};
    if (!ifs) [[unlikely]] {
        JS_ThrowPlainError(engine->context_, "module not found: %s", path.string().c_str());
        return nullptr;
    }
    auto buffer = std::vector<uint8_t>((std::istreambuf_iterator<char>{ifs}), std::istreambuf_iterator<char>{});
    ifs.close();

    auto v = JS_ReadObject(engine->context_, buffer.data(), buffer.size(), JS_READ_OBJ_BYTECODE);
    if (JS_IsException(v)) {
        return nullptr;
    }
    if (JS_VALUE_GET_TAG(v) == JS_TAG_MODULE) {
        if (JS_ResolveModule(engine->context_, v) < 0) {
            JS_FreeValue(engine->context_, v);
            JS_ThrowPlainError(engine->context_, "failed to resolve module %s", specifier);
            return nullptr;
        }
        auto m = (JSModuleDef*)JS_VALUE_GET_PTR(v);
        JS_FreeValue(engine->context_, v);
        (void)setImportMeta(engine->context_, m, specifier);
        return m;
    }
    JS_FreeValue(engine->context_, v);
    JS_ThrowPlainError(engine->context_, "cannot load module %s", specifier);
    return nullptr;
}
JSModuleDef*
QjsModuleLoader::performLoadScriptModule(Engine* engine, std::filesystem::path const& path, const char* specifier) {
    auto ifs = std::ifstream{path};
    if (!ifs) [[unlikely]] {
        JS_ThrowPlainError(engine->context_, "module not found: %s", path.string().c_str());
        return nullptr;
    }
    auto source = std::string{std::istreambuf_iterator<char>{ifs}, std::istreambuf_iterator<char>{}};
    ifs.close();

    auto result = JS_Eval(
        engine->context_,
        source.c_str(),
        source.size(),
        specifier,
        JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY
    );
    if (JS_IsException(result)) {
        return nullptr;
    }
    auto m = (JSModuleDef*)JS_VALUE_GET_PTR(result);
    (void)setImportMeta(engine->context_, m, specifier);
    JS_FreeValue(engine->context_, result);
    return m;
}
JSModuleDef* QjsModuleLoader::performNewNativeModule(Engine* engine, ModuleMeta const* meta) {
    auto def      = JS_NewCModule(engine->context_, meta->name_.c_str(), [](JSContext* ctx, JSModuleDef* def) -> int {
        auto external      = JS_GetModulePrivateValue(ctx, def);
        auto pointerDataID = JS_GetClassID(external);
        auto engine        = static_cast<Engine*>(JS_GetOpaque(external, pointerDataID));
        JS_FreeValue(ctx, external);

        assert(engine);
        assert(engine->pointerDataClassId_ == pointerDataID);

        EngineScope tracker{engine}; // for addon
        return performModuleExports(engine, def);
    });
    if (!def) {
        return nullptr;
    }
    auto external = engine->newPointerData(engine);
    JS_SetModulePrivateValue(engine->context_, def, external);

    engine->module2meta_.emplace(def, meta);

    performDeclareModuleExports(engine, def, meta);
    return def;
}
void QjsModuleLoader::performDeclareModuleExports(Engine* engine, JSModuleDef* def, ModuleMeta const* meta) {
    if (meta->hasDefaultExport()) {
        JS_AddModuleExport(engine->context_, def, kDefaultExport.data());
    }
    for (auto& v : meta->constants_) {
        QjsHelper::rethrowException(JS_AddModuleExport(engine->context_, def, v.name_.c_str()));
    }
    for (auto& f : meta->functions_) {
        QjsHelper::rethrowException(JS_AddModuleExport(engine->context_, def, f.name_.c_str()));
    }

    std::unordered_set<std::string> declaredRoots;

    auto addExport = [&](const std::string& fullName) {
        if (auto root = namespace_utils::getNamespaceRoot(fullName)) {
            // NOTE: getNamespaceRoot internally substr std::string,
            // but quickjs internally strlen relies on \0 terminators.
            // Explicit copy to std::string, making sure the string \0 is correct.
            std::string rootStr(*root);
            if (!declaredRoots.contains(rootStr)) {
                declaredRoots.insert(rootStr);
                QjsHelper::rethrowException(JS_AddModuleExport(engine->context_, def, rootStr.c_str()));
            }
        } else {
            QjsHelper::rethrowException(JS_AddModuleExport(engine->context_, def, fullName.c_str()));
        }
    };

    for (auto& c : meta->classes_) {
        addExport(c->name_);
    }
    for (auto& e : meta->enums_) {
        addExport(e->name_);
    }
}
int QjsModuleLoader::performModuleExports(Engine* engine, JSModuleDef* def) try {
    auto iter = engine->module2meta_.find(def);
    if (iter == engine->module2meta_.end()) {
        return -1;
    }
    auto meta = iter->second;

    if (meta->hasDefaultExport()) {
        auto v = std::visit(
            [&](auto& arg) -> JSValue {
                using type = std::remove_cvref_t<decltype(arg)>;
                if constexpr (std::is_same_v<type, ClassMeta const*>) {
                    auto typed = static_cast<ClassMeta const*>(arg);
                    if (!engine->getClassMeta(typed->name_)) {
                        engine->performRegisterClass(*typed);
                    }
                    if (typed->hasConstructor()) {
                        auto& pair = engine->classConstructors_.at(typed);
                        return JS_DupValue(engine->context_, pair.first);
                    } else {
                        auto& obj = engine->staticClassObject_.at(typed);
                        return JS_DupValue(engine->context_, obj);
                    }
                } else if constexpr (std::is_same_v<type, EnumMeta const*>) {
                    auto typed = static_cast<EnumMeta const*>(arg);
                    if (!engine->getEnumMeta(typed->name_)) {
                        engine->performRegisterEnum(*typed);
                    }
                    auto& obj = engine->enumObject_.at(typed);
                    return QjsHelper::getDupLocal(obj.get(), engine->context_);

                } else if constexpr (std::is_same_v<type, GetterCallback>) {
                    auto&        getter = static_cast<GetterCallback const&>(arg);
                    Local<Value> val    = getter();
                    return QjsHelper::getDupLocal(val, engine->context_);

                } else if constexpr (std::is_same_v<type, FunctionCallback>) {
                    auto& callback = static_cast<FunctionCallback const&>(arg);
                    auto  cb       = engine->newDataFunction(
                        [](Arguments const& args, void* data1, void*) -> Local<Value> {
                            auto callback = static_cast<FunctionCallback const*>(data1);
                            return (*callback)(args);
                        },
                        const_cast<FunctionCallback*>(&callback),
                        nullptr
                    );
                    return QjsHelper::getDupLocal(cb, engine->context_);
                } else {
                    [[unlikely]] throw std::logic_error(
                        "jspp internal error: unhandled default export type. "
                        "This is a framework bug, please report it."
                    );
                }
            },
            meta->default_
        );
        JS_SetModuleExport(engine->context_, def, kDefaultExport.data(), v);
    }

    for (auto& v : meta->constants_) {
        Local<Value> val = v.getter_();
        JS_SetModuleExport(engine->context_, def, v.name_.c_str(), QjsHelper::getDupLocal(val, engine->context_));
    }

    for (auto& f : meta->functions_) {
        auto cb = engine->newDataFunction(
            [](Arguments const& args, void* data1, void*) -> Local<Value> {
                auto fnMeta = static_cast<ModuleFunctionExport const*>(data1);
                return fnMeta->callback_(args);
            },
            const_cast<ModuleFunctionExport*>(&f),
            nullptr
        );
        JS_SetModuleExport(engine->context_, def, f.name_.c_str(), QjsHelper::getDupLocal(cb, engine->context_));
    }


    std::unordered_map<std::string_view, Local<Object>> namespaceRoots{};

    auto exportNamespaceObj = [&](std::string_view fullNs, Local<Value> val) {
        if (auto root = namespace_utils::getNamespaceRoot(fullNs)) {
            auto it = namespaceRoots.find(*root);
            if (it == namespaceRoots.end()) {
                auto rootNamespace = Object::newObject();
                JS_SetModuleExport(
                    engine->context_,
                    def,
                    std::string{*root}.c_str(),
                    QjsHelper::getDupLocal(rootNamespace, engine->context_)
                );
                it = namespaceRoots.emplace(*root, rootNamespace).first;
            }
            auto namespaceObj = it->second;

            auto ns = namespace_utils::skipFirstSegment(fullNs);
            namespace_utils::mountNamespace(namespaceObj, ns, val);

        } else { // without namespace
            JS_SetModuleExport(engine->context_, def, fullNs.data(), QjsHelper::getDupLocal(val, engine->context_));
        }
    };

    for (auto c : meta->classes_) {
        if (!engine->getClassMeta(c->name_)) {
            engine->performRegisterClass(*c);
        }
        Local<Value> ctor{};
        if (c->hasConstructor()) {
            auto& pair = engine->classConstructors_.at(c);
            ctor       = ValueHelper::wrap<Value>(JS_DupValue(engine->context_, pair.first));
        } else {
            auto& obj = engine->staticClassObject_.at(c);
            ctor      = ValueHelper::wrap<Value>(JS_DupValue(engine->context_, obj));
        }
        exportNamespaceObj(c->name_, ctor);
    }
    for (auto e : meta->enums_) {
        if (!engine->getEnumMeta(e->name_)) {
            engine->performRegisterEnum(*e);
        }
        auto& obj = engine->enumObject_.at(e);
        exportNamespaceObj(e->name_, obj.get());
    }
    return 0;
} catch (Exception const& e) {
    (void)QjsHelper::rethrowToScript(e, engine);
    return -1;
} catch (std::exception const& e) {
    (void)QjsHelper::rethrowToScript(e, engine);
    return -1;
} catch (...) {
    JS_ThrowPlainError(engine->context_, "Unknown C++ exception occurred");
    return -1;
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
Local<Value> Engine::evalModule(Local<String> const& code, Local<String> const& source) {
    if (hasFlag(flags_, qjs_backend::QjsInitializeFlags::NoModuleLoader)) {
        throw std::logic_error("evalModule/registerModule is not supported in AddonMode or NoModuleLoader");
    }
    auto stdCode   = code.getValue();
    auto stdSource = source.getValue();
    auto result =
        JS_Eval(context_, stdCode.data(), stdCode.size(), stdSource.data(), JS_EVAL_FLAG_STRICT | JS_EVAL_TYPE_MODULE);
    qjs_backend::QjsHelper::rethrowException(result);

    // check promise rejected
    JSPromiseStateEnum state = JS_PromiseState(context_, result);
    if (state == JSPromiseStateEnum::JS_PROMISE_REJECTED) {
        JSValue msg = JS_PromiseResult(context_, result);
        JS_FreeValue(context_, result);               // Release the execution result
        JS_Throw(context_, msg);                      // Throw a pending exception
        qjs_backend::QjsHelper::rethrowException(-1); // Handle pending exception
    }

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

void Engine::registerModule(ModuleMeta const& meta) {
    if (hasFlag(flags_, qjs_backend::QjsInitializeFlags::NoModuleLoader)) {
        throw std::logic_error(
            "registerModule is not available in Addon or NoModuleLoader mode. "
            "In Addon mode, a module is instantiated directly via the engine's entry point."
        );
    }
    if (registeredModule_.contains(meta.name_)) [[unlikely]] {
        throw std::logic_error("Module already registered: " + meta.name_);
    }
    registeredModule_.emplace(meta.name_, &meta);
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
    auto id = JS_GetClassID(value);
    if (id == JS_INVALID_CLASS_ID) return;

    // Since this callback is only bound to the class we registered, Opaque must be InstancePayload
    if (auto opaque = JS_GetOpaque(value, id)) {
        auto payload = static_cast<InstancePayload*>(opaque);
        if (auto trampoline = payload->getHolder().get_trampoline()) {
            // If there is a trampoline and a JS object is held
            if (trampoline->object_ && !trampoline->object_->weak().isEmpty()) {
                // Directly take out the bottom-level raw JSValue and pass it to JS_MarkValue
                //  without triggering refcount changes
                JSValue raw_val = trampoline->object_->weak().impl.ref_;
                JS_MarkValue(runtime, raw_val, markFunc);
            }
        }
    }
}

Local<Value> Engine::performRegisterClass(ClassMeta const& meta) {
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
        engine->registeredClasses_.emplace(meta.name_, &meta);
        engine->staticClassObject_.emplace(&meta, qjs_backend::QjsHelper::getDupLocal(object, engine->context_));
        return object;
    }

    JSClassDef classDef{};
    classDef.class_name = namespace_utils::getNamespaceLeafString(meta.name_).data();
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
    return ctor;
}

Local<Object> Engine::performRegisterEnum(EnumMeta const& meta) {
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
    enumObject_.emplace(&meta, object);
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
    auto engine    = asEngine();
    auto ctx       = engine->context_;
    auto parentVal = qjs_backend::QjsHelper::peekValue(parentObj);
    auto subVal    = qjs_backend::QjsHelper::peekValue(subObj);

    if (JS_IsException(parentVal) || JS_IsException(subVal)) {
        return false;
    }

    // Define a hidden property on subObj that points to parentVal
    int ret = JS_DefinePropertyValue(
        ctx,
        subVal,
        engine->referenceInternalAtom_,
        qjs_backend::QjsHelper::dupValue(parentVal, ctx),
        0 // Indicates that this property is non-enumerable, non-writable, and non-configurable
    );

    if (ret < 0) {
        qjs_backend::QjsHelper::rethrowException(ret);
        return false;
    }
    return true;
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

JSValue QjsEngine::newPointerData(void* ptr) const {
    if (!ptr) {
        return JS_NULL;
    }
    auto data = JS_NewObjectClass(context_, pointerDataClassId_);
    QjsHelper::rethrowException(data);
    JS_SetOpaque(data, ptr);
    return data;
}

Local<Function> QjsEngine::newDataFunction(DataFunctionCallback callback, void* data1, void* data2) {
    auto engine = asEngine();

    auto    opaqueData1    = newPointerData(data1);
    auto    opaqueData2    = newPointerData(data2);
    auto    opaqueCallback = newPointerData(reinterpret_cast<void*>(callback));
    JSValue engineOpaque   = newPointerData(engine);

    auto args = std::array{opaqueData1, opaqueData2, opaqueCallback, engineOpaque};

    auto fn = JS_NewCFunctionData(
        context_,
        [](JSContext* ctx, JSValueConst thiz, int argc, JSValueConst* argv, int /* magic */, JSValueConst* data
        ) -> JSValue {
            auto externalEngine = data[3];
            auto externalID     = JS_GetClassID(externalEngine);
            auto engine         = static_cast<Engine*>(JS_GetOpaque(externalEngine, externalID));

            if (engine->pointerDataClassId_ != externalID) [[unlikely]] {
                JS_ThrowTypeError(ctx, "Invalid engine pointer data");
                return JS_EXCEPTION;
            }

            auto data1    = JS_GetOpaque(data[0], externalID);
            auto data2    = JS_GetOpaque(data[1], externalID);
            auto callback = reinterpret_cast<DataFunctionCallback>(JS_GetOpaque(data[2], externalID));

            EngineScope tracker{engine}; // for addon
            try {
                auto args = QjsHelper::wrapArguments(engine, thiz, argc, argv);
                auto ret  = callback(args, data1, data2);
                return QjsHelper::getDupLocal(ret, engine->context_);
            } catch (Exception const& e) {
                return QjsHelper::rethrowToScript(e, engine);
            } catch (std::exception const& e) {
                return QjsHelper::rethrowToScript(e, engine);
            } catch (...) {
                JS_ThrowPlainError(engine->context_, "Unknown C++ exception occurred");
                return JS_EXCEPTION;
            }
        },
        0,
        0,
        args.size(),
        args.data()
    );

    for (auto& arg : args) {
        JS_FreeValue(context_, arg);
    }

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

bool QjsEngine::CheckPrototypeSignature(ClassMeta const* meta, ClassMeta const* target) {
    // TODO(optimization): For each ClassMeta, maintain a cached unordered_set of all ancestor classes.
    // Then implement isFamily(target) to quickly check if a class is derived from target.
    // This avoids repeatedly walking the inheritance chain in high-frequency calls.
    while (meta) {
        if (meta == target) {
            return true;
        }
        meta = meta->base_;
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

                if (!CheckPrototypeSignature(payload->getDefine(), meta)) [[unlikely]] {
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

                if (!CheckPrototypeSignature(payload->getDefine(), meta)) [[unlikely]] {
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

                    if (!CheckPrototypeSignature(payload->getDefine(), meta)) [[unlikely]] {
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