#include "V8Engine.h"

#include "V8Helper.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/InstancePayload.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/Reference.h"
#include "jspp/core/TrackedHandle.h"
#include "jspp/core/Utils.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

#include <cassert>
#include <concepts>
#include <fstream>
#include <ranges>
#include <type_traits>
#include <unordered_set>

JSPP_WARNING_GUARD_BEGIN
#include "v8-exception.h"
#include <v8-context.h>
#include <v8-exception.h>
#include <v8-external.h>
#include <v8-function-callback.h>
#include <v8-isolate.h>
#include <v8-local-handle.h>
#include <v8-locker.h>
#include <v8-message.h>
#include <v8-object.h>
#include <v8-persistent-handle.h>
#include <v8-primitive.h>
#include <v8-script.h>
#include <v8-template.h>
#include <v8-value.h>
JSPP_WARNING_GUARD_END


namespace jspp {
namespace v8_backend {
static_assert(std::is_base_of_v<V8Engine, Engine>, "Engine must be derived from V8Engine");
static_assert(std::is_final_v<Engine>, "Engine must be final");

Engine*       V8Engine::asEngine() { return static_cast<Engine*>(this); }
Engine const* V8Engine::asEngine() const { return static_cast<Engine const*>(this); }

void V8Engine::ensureInitializeFlags(V8InitializeFlags flags) {
    bool requiredModuleLoader = !hasFlag(flags, V8InitializeFlags::NoModuleLoader);
    bool requiredContextData  = !hasFlag(flags, V8InitializeFlags::NoContextData);
    if (requiredModuleLoader && !requiredContextData) {
        [[unlikely]] throw std::logic_error{"V8Engine Error: ModuleLoader depends on ContextData. "
                                            "You cannot set NoContextData without also setting NoModuleLoader."};
    }
}

#ifndef JSPP_V8_CONTEXT_EMBEDDER_DATA_INDEX
#define JSPP_V8_CONTEXT_EMBEDDER_DATA_INDEX 1
#endif

/**
 * Context Embedder Data slot allocation.
 *
 * Slot 0 is reserved by V8 for the Chrome DevTools debugger (v8::Context documentation comments).
 * Node.js internal slots start from 32 (src/node_context_data.h).
 * Therefore Slots 1-31 are the safe zone, and jspp chooses Slot 1.
 *
 * In Addon mode, jspp does not access ContextData (Engine* is passed through module closure), absolutely safe.
 * In Host Embed mode, conditionally safe. The user must ensure the host does not use Slot 1,
 * and can override it via JSPP_V8_CONTEXT_EMBEDDER_DATA_INDEX.
 *
 * @review 2026-05-06T00:00:00Z
 */
constexpr int kEngineContextEmbedderDataIndex = JSPP_V8_CONTEXT_EMBEDDER_DATA_INDEX;

void V8Engine::initContext() {
    constructorSymbol_ = v8::Global<v8::Symbol>(isolate_, v8::Symbol::New(isolate_));
    nativeFunctionTag_ = v8::Global<v8::Private>(isolate_, v8::Private::New(isolate_));

    auto localExternalPair = v8::ObjectTemplate::New(isolate_);
    localExternalPair->SetInternalFieldCount(static_cast<int>(ExternalPairInternalField::Count));
    externalPairTemplate_.Reset(isolate_, localExternalPair);

    if (!hasFlag(flags_, V8InitializeFlags::NoContextData)) {
        auto ctx = context_.Get(isolate_);
        ctx->SetAlignedPointerInEmbedderData(kEngineContextEmbedderDataIndex, this);
    }

    if (!hasFlag(flags_, V8InitializeFlags::NoModuleLoader)) {
        isolate_->SetHostImportModuleDynamicallyCallback(V8ModuleLoader::HostImportModuleDynamicallyCallback);
        isolate_->SetHostInitializeImportMetaObjectCallback(V8ModuleLoader::HostInitializeImportMetaObjectCallback);
    }
}

V8Engine::V8Engine(V8IsolateFactory const& factory, V8InitializeFlags flags) : flags_(flags) {
    ensureInitializeFlags(flags);
    if (factory) {
        isolate_ = factory();
        if (!isolate_) throw std::runtime_error("Failed to create V8 isolate");
    } else {
        v8::Isolate::CreateParams params;
        params.array_buffer_allocator = v8::ArrayBuffer::Allocator::NewDefaultAllocator();

        isolate_ = v8::Isolate::New(params);
    }
    v8::Locker         locker(isolate_);
    v8::Isolate::Scope isolate_scope(isolate_);
    v8::HandleScope    handle_scope(isolate_);
    context_.Reset(isolate_, v8::Context::New(isolate_));

    isolate_->SetCaptureStackTraceForUncaughtExceptions(true);
    initContext();
}

V8Engine::V8Engine(v8::Isolate* isolate, v8::Local<v8::Context> ctx, V8InitializeFlags flags)
: isolate_(isolate),
  context_(v8::Global<v8::Context>{isolate, ctx}),
  flags_(flags) {
    ensureInitializeFlags(flags);
    // todo ensure engine scope is active?
    initContext();
}

V8Engine::~V8Engine() { dispose(); }

void V8Engine::dispose() {
    if (context_.IsEmpty()) return; // Already disposed
    {
        EngineScope scope(asEngine());

        for (auto& [manager, value] : managedResources_) {
            value.Reset();
            manager->deleter(manager->resource);
            delete manager;
        }
        for (auto& ctor : classConstructors_ | std::views::values) {
            ctor.Reset();
        }
        for (auto& pair : loadedModules_ | std::views::values) {
            pair.Reset();
        }
        for (auto& pair : enumObject_ | std::views::values) {
            pair.Reset();
        }

        externalPairTemplate_.Reset();
        constructorSymbol_.Reset();
        nativeFunctionTag_.Reset();
        classConstructors_.clear();
        managedResources_.clear();
        loadedModules_.clear();
        moduleIdentityMap_.clear();
        syntheticModules_.clear();
        enumObject_.clear();
        context_.Reset();
    }

    if (!hasFlag(flags_, V8InitializeFlags::NoDisposeIsolate)) {
        isolate_->Dispose();
    }
}

v8::Isolate*           V8Engine::isolate() const { return isolate_; }
v8::Local<v8::Context> V8Engine::context() const { return context_.Get(isolate_); }

void V8Engine::addManagedResource(void* resource, v8::Local<v8::Value> value, std::function<void(void*)>&& deleter) {
    auto managed = std::make_unique<ManagedResource>(asEngine(), resource, std::move(deleter));

    v8::Global<v8::Value> weak{isolate_, value};
    weak.SetWeak(
        static_cast<void*>(managed.get()),
        [](v8::WeakCallbackInfo<void> const& data) {
            auto managed = static_cast<ManagedResource*>(data.GetParameter());
            auto runtime = managed->runtime;
            {
                v8::Locker locker(runtime->isolate_); // Since the v8 GC is not on the same thread, locking is required
                auto       iter = runtime->managedResources_.find(managed);
                assert(iter != runtime->managedResources_.end()); // ManagedResource should be in the map
                iter->second.Reset();
                runtime->managedResources_.erase(iter);

                data.SetSecondPassCallback([](v8::WeakCallbackInfo<void> const& data) {
                    auto       managed = static_cast<ManagedResource*>(data.GetParameter());
                    v8::Locker locker(managed->runtime->isolate_);
                    managed->deleter(managed->resource);
                    delete managed;
                });
            }
        },
        v8::WeakCallbackType::kParameter
    );
    managedResources_.emplace(managed.release(), std::move(weak));
}

std::string V8ModuleLoader::normalizePath(const std::string& importingModule, const std::string& importSpecifier) {
    constexpr std::string_view kFileUrlPrefix = "file://";
    std::string_view           sv_source      = importingModule;
    std::string_view           sv_specifier   = importSpecifier;

    bool is_relative = sv_specifier.starts_with("./") || sv_specifier.starts_with("../");
    bool is_absolute = std::filesystem::path(sv_specifier).is_absolute();
    bool is_file_url = sv_specifier.starts_with(kFileUrlPrefix);

    if (!is_relative && !is_absolute && !is_file_url) {
        return std::string(sv_specifier);
    }

    std::filesystem::path final_path;

    if (is_absolute) {
        final_path = sv_specifier;
    } else if (is_file_url) {
        std::string p{sv_specifier};
        p.erase(0, kFileUrlPrefix.size());
#ifdef _WIN32
        if (p.starts_with("/")) p.erase(0, 1);
#endif
        final_path = p;
    } else {
        std::string cp_base{sv_source};
        if (cp_base.starts_with(kFileUrlPrefix)) {
            cp_base.erase(0, kFileUrlPrefix.size());
#ifdef _WIN32
            if (cp_base.starts_with("/")) cp_base.erase(0, 1);
#endif
        } else if (cp_base.empty() || cp_base.starts_with("<eval>")) {
            // The dynamic reference executed by <eval> defaults to the current working directory
            cp_base = std::filesystem::current_path().generic_string();
        } else if (!std::filesystem::path(cp_base).is_absolute()) {
            cp_base = std::filesystem::current_path().generic_string();
        }
        std::filesystem::path base_path{cp_base};
        final_path = base_path.parent_path() / sv_specifier;
    }

    std::error_code err;
    final_path = std::filesystem::weakly_canonical(final_path, err);
    if (err || !std::filesystem::is_regular_file(final_path)) {
        throw std::runtime_error("module not found: " + importSpecifier);
    }

    std::string gen_path  = final_path.generic_string();
    std::string final_url = "file://";
    if (!gen_path.starts_with("/")) final_url += "/";
    final_url += gen_path;

    return final_url;
}

v8::MaybeLocal<v8::Module> V8ModuleLoader::performLoadFileModule(Engine* engine, const std::string& resolvedUrl) {
    auto isolate = engine->isolate();

    std::string                filePath       = resolvedUrl;
    constexpr std::string_view kFileUrlPrefix = "file://";
    if (filePath.starts_with(kFileUrlPrefix)) {
        filePath.erase(0, kFileUrlPrefix.size());
#ifdef _WIN32
        if (filePath.starts_with("/")) filePath.erase(0, 1);
#endif
    }

    std::ifstream ifs(filePath);
    if (!ifs.is_open()) {
        isolate->ThrowException(
            v8::Exception::Error(
                v8::String::NewFromUtf8(isolate, ("module not found: " + resolvedUrl).c_str()).ToLocalChecked()
            )
        );
        return v8::MaybeLocal<v8::Module>();
    }
    std::string sourceStr((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    auto v8Source = v8::String::NewFromUtf8(isolate, sourceStr.c_str(), v8::NewStringType::kNormal, sourceStr.size())
                        .ToLocalChecked();
    auto v8ResourceName =
        v8::String::NewFromUtf8(isolate, resolvedUrl.c_str(), v8::NewStringType::kNormal, resolvedUrl.size())
            .ToLocalChecked();

    v8::ScriptOrigin           origin(v8ResourceName, 0, 0, false, -1, v8::Local<v8::Value>(), false, false, true);
    v8::ScriptCompiler::Source compilerSource(v8Source, origin);

    v8::Local<v8::Module> module;
    if (!v8::ScriptCompiler::CompileModule(isolate, &compilerSource).ToLocal(&module)) {
        return v8::MaybeLocal<v8::Module>();
    }

    return module;
}
v8::Local<v8::Module> V8ModuleLoader::performNewNativeModule(Engine* engine, ModuleMeta const* meta) {
    auto                               isolate = engine->isolate();
    std::vector<v8::Local<v8::String>> export_names;

    if (meta->hasDefaultExport()) {
        export_names.push_back(v8::String::NewFromUtf8Literal(isolate, "default"));
    }
    for (const auto& c : meta->constants_) {
        export_names.push_back(v8::String::NewFromUtf8(isolate, c.name_.c_str()).ToLocalChecked());
    }
    for (const auto& f : meta->functions_) {
        export_names.push_back(v8::String::NewFromUtf8(isolate, f.name_.c_str()).ToLocalChecked());
    }

    std::unordered_set<std::string> declaredRoots;
    auto                            addExport = [&](const std::string& fullName) {
        if (auto root = namespace_utils::getNamespaceRoot(fullName)) {
            std::string rootStr(*root);
            if (!declaredRoots.contains(rootStr)) {
                declaredRoots.insert(rootStr);
                export_names.push_back(v8::String::NewFromUtf8(isolate, rootStr.c_str()).ToLocalChecked());
            }
        } else {
            export_names.push_back(v8::String::NewFromUtf8(isolate, fullName.c_str()).ToLocalChecked());
        }
    };

    for (const auto& c : meta->classes_) addExport(c->name_);
    for (const auto& e : meta->enums_) addExport(e->name_);

    auto module_name = v8::String::NewFromUtf8(isolate, meta->name_.c_str()).ToLocalChecked();
    v8::MemorySpan<const v8::Local<v8::String>> span(export_names.data(), export_names.size());

    // Create a V8 synthetic module to proxy native objects
    auto module = v8::Module::CreateSyntheticModule(isolate, module_name, span, SyntheticModuleEvaluationSteps);

    engine->syntheticModules_.emplace(module->GetIdentityHash(), meta);
    return module;
}

v8::MaybeLocal<v8::Value>
V8ModuleLoader::SyntheticModuleEvaluationSteps(v8::Local<v8::Context> context, v8::Local<v8::Module> module) {
    auto isolate = context->GetIsolate();
    auto engine  = static_cast<Engine*>(context->GetAlignedPointerFromEmbedderData(kEngineContextEmbedderDataIndex));

    ModuleMeta const* meta = nullptr;
    int hash = module->GetIdentityHash();
    if (auto it = engine->syntheticModules_.find(hash); it != engine->syntheticModules_.end()) {
        meta = it->second;
    }

    if (!meta) {
        isolate->ThrowException(
            v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "Native module meta not found"))
        );
        return v8::MaybeLocal<v8::Value>();
    }

    try {
        EngineScope scope(engine);

        if (meta->hasDefaultExport()) {
            v8::Local<v8::Value> defaultVal;
            std::visit(
                [&](auto&& arg) {
                    using T = std::decay_t<decltype(arg)>;
                    if constexpr (std::is_same_v<T, ClassMeta const*>) {
                        if (!engine->getClassMeta(arg->name_)) engine->performRegisterClass(*arg);
                        defaultVal =
                            engine->classConstructors_.at(arg).Get(isolate)->GetFunction(context).ToLocalChecked();
                    } else if constexpr (std::is_same_v<T, EnumMeta const*>) {
                        if (!engine->getEnumMeta(arg->name_)) engine->performRegisterEnum(*arg);
                        defaultVal = engine->enumObject_.at(arg).Get(isolate);
                    } else if constexpr (std::is_same_v<T, GetterCallback>) {
                        defaultVal = ValueHelper::unwrap(arg());
                    } else if constexpr (std::is_same_v<T, FunctionCallback>) {
                        auto func  = Function::newFunction([cb = arg](Arguments const& args) { return cb(args); });
                        defaultVal = ValueHelper::unwrap(func);
                    }
                },
                meta->default_
            );
            if (!defaultVal.IsEmpty()) {
                module
                    ->SetSyntheticModuleExport(isolate, v8::String::NewFromUtf8Literal(isolate, "default"), defaultVal)
                    .Check();
            }
        }

        for (const auto& v : meta->constants_) {
            auto                  val = v.getter_();
            v8::Local<v8::String> key;
            if (!v8::String::NewFromUtf8(isolate, v.name_.c_str()).ToLocal(&key)) {
                return v8::MaybeLocal<v8::Value>();
            }
            module->SetSyntheticModuleExport(isolate, key, ValueHelper::unwrap(val)).Check();
        }

        for (const auto& f : meta->functions_) {
            auto func = Function::newFunction([cb = f.callback_](Arguments const& args) { return cb(args); });
            v8::Local<v8::String> key;
            if (!v8::String::NewFromUtf8(isolate, f.name_.c_str()).ToLocal(&key)) {
                return v8::MaybeLocal<v8::Value>();
            }
            module->SetSyntheticModuleExport(isolate, key, ValueHelper::unwrap(func)).Check();
        }

        std::unordered_map<std::string_view, v8::Local<v8::Object>> namespaceRoots;
        bool                                                        has_pending_exception = false;

        auto exportNamespaceObj = [&](std::string_view fullNs, v8::Local<v8::Value> val) {
            if (has_pending_exception) return;
            if (auto root = namespace_utils::getNamespaceRoot(fullNs)) {
                auto it = namespaceRoots.find(*root);
                if (it == namespaceRoots.end()) {
                    auto                  rootNamespace = v8::Object::New(isolate);
                    v8::Local<v8::String> key;
                    if (!v8::String::NewFromUtf8(isolate, std::string(*root).c_str()).ToLocal(&key)) {
                        has_pending_exception = true;
                        return;
                    }
                    module->SetSyntheticModuleExport(isolate, key, rootNamespace).Check();
                    it = namespaceRoots.emplace(*root, rootNamespace).first;
                }
                auto namespaceObj = it->second;
                auto ns           = namespace_utils::skipFirstSegment(fullNs);
                namespace_utils::mountNamespace(
                    ValueHelper::wrap<Object>(namespaceObj),
                    ns,
                    ValueHelper::wrap<Value>(val)
                );
            } else {
                v8::Local<v8::String> key;
                if (!v8::String::NewFromUtf8(isolate, fullNs.data()).ToLocal(&key)) {
                    has_pending_exception = true;
                    return;
                }
                module->SetSyntheticModuleExport(isolate, key, val).Check();
            }
        };

        for (auto c : meta->classes_) {
            if (!engine->getClassMeta(c->name_)) engine->performRegisterClass(*c);
            auto ctor = engine->classConstructors_.at(c).Get(isolate)->GetFunction(context).ToLocalChecked();
            exportNamespaceObj(c->name_, ctor);
            if (has_pending_exception) return v8::MaybeLocal<v8::Value>();
        }
        for (auto e : meta->enums_) {
            if (!engine->getEnumMeta(e->name_)) engine->performRegisterEnum(*e);
            auto enumObj = engine->enumObject_.at(e).Get(isolate);
            exportNamespaceObj(e->name_, enumObj);
            if (has_pending_exception) return v8::MaybeLocal<v8::Value>();
        }

        auto resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
        resolver->Resolve(context, v8::Undefined(isolate)).Check();
        return resolver->GetPromise();
    } catch (Exception const& e) {
        v8_backend::V8Helper::rethrowToScript(e);
        return v8::MaybeLocal<v8::Value>();
    } catch (std::exception const& e) {
        v8_backend::V8Helper::rethrowToScript(e);
        return v8::MaybeLocal<v8::Value>();
    } catch (...) {
        isolate->ThrowException(
            v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "Unknown C++ exception occurred"))
        );
        return v8::MaybeLocal<v8::Value>();
    }
}
v8::MaybeLocal<v8::Promise> V8ModuleLoader::HostImportModuleDynamicallyCallback(
    v8::Local<v8::Context>    context,
    v8::Local<v8::Data>       hostDefinedOptions,
    v8::Local<v8::Value>      resourceName,
    v8::Local<v8::String>     specifier,
    v8::Local<v8::FixedArray> importAttributes
) {
    auto isolate = context->GetIsolate();
    auto engine  = static_cast<Engine*>(context->GetAlignedPointerFromEmbedderData(kEngineContextEmbedderDataIndex));

    auto         resolver = v8::Promise::Resolver::New(context).ToLocalChecked();
    v8::TryCatch try_catch(isolate);

    std::string referrerUrl = "";
    if (!resourceName.IsEmpty() && resourceName->IsString()) {
        v8::String::Utf8Value urlUtf8(isolate, resourceName.As<v8::String>());
        referrerUrl = std::string(*urlUtf8, urlUtf8.length());
    }

    v8::String::Utf8Value specifierUtf8(isolate, specifier);
    std::string           specifierStr(*specifierUtf8, specifierUtf8.length());

    auto getFallbackException = [&]() -> v8::Local<v8::Value> {
        return try_catch.HasCaught()
                 ? try_catch.Exception()
                 : v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "Execution Terminated"));
    };

    try {
        std::string           resolvedUrl = normalizePath(referrerUrl, specifierStr);
        v8::Local<v8::Module> module;

        if (auto meta = engine->getModuleMeta(resolvedUrl)) {
            if (auto it = engine->loadedModules_.find(resolvedUrl); it != engine->loadedModules_.end()) {
                module = v8::Local<v8::Module>::New(isolate, it->second);
            } else {
                module = performNewNativeModule(engine, meta);
                engine->loadedModules_.emplace(resolvedUrl, v8::Global<v8::Module>(isolate, module));
                engine->moduleIdentityMap_.emplace(module->GetIdentityHash(), resolvedUrl); // [新增] O(1) 逆向哈希注册
            }
        } else {
            if (auto it = engine->loadedModules_.find(resolvedUrl); it != engine->loadedModules_.end()) {
                module = v8::Local<v8::Module>::New(isolate, it->second);
            } else {
                auto maybeModule = performLoadFileModule(engine, resolvedUrl);
                if (!maybeModule.ToLocal(&module)) {
                    resolver->Reject(context, getFallbackException()).Check();
                    return resolver->GetPromise();
                }
                engine->loadedModules_.emplace(resolvedUrl, v8::Global<v8::Module>(isolate, module));
                engine->moduleIdentityMap_.emplace(module->GetIdentityHash(), resolvedUrl); // [新增] O(1) 逆向哈希注册
            }
        }

        // Instantiate dependency graph
        if (module->GetStatus() == v8::Module::kUninstantiated) {
            if (module->InstantiateModule(context, ResolveModuleCallback).IsNothing()) {
                resolver->Reject(context, getFallbackException()).Check();
                return resolver->GetPromise();
            }
        }

        v8::Local<v8::Value> evalResult;
        if (!module->Evaluate(context).ToLocal(&evalResult)) {
            resolver->Reject(context, getFallbackException()).Check();
            return resolver->GetPromise();
        }

        auto ns = module->GetModuleNamespace();

        // Avoid Top-Level Await timing gaps and link Promise states
        if (evalResult->IsPromise()) {
            auto promise = evalResult.As<v8::Promise>();
            if (promise->State() == v8::Promise::kFulfilled) {
                resolver->Resolve(context, ns).Check();
            } else if (promise->State() == v8::Promise::kRejected) {
                resolver->Reject(context, promise->Result()).Check();
            } else {
                auto dataArr = v8::Array::New(isolate, 2);
                dataArr->Set(context, 0, resolver).Check();
                dataArr->Set(context, 1, ns).Check();

                auto resolveHandler = v8::Function::New(
                                          context,
                                          [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                                              auto ctx     = info.GetIsolate()->GetCurrentContext();
                                              auto dataArr = info.Data().As<v8::Array>();
                                              auto res =
                                                  dataArr->Get(ctx, 0).ToLocalChecked().As<v8::Promise::Resolver>();
                                              auto namespaceObj = dataArr->Get(ctx, 1).ToLocalChecked();
                                              res->Resolve(ctx, namespaceObj).Check();
                                          },
                                          dataArr
                )
                                          .ToLocalChecked();

                auto rejectHandler = v8::Function::New(
                                         context,
                                         [](const v8::FunctionCallbackInfo<v8::Value>& info) {
                                             auto ctx     = info.GetIsolate()->GetCurrentContext();
                                             auto dataArr = info.Data().As<v8::Array>();
                                             auto res =
                                                 dataArr->Get(ctx, 0).ToLocalChecked().As<v8::Promise::Resolver>();
                                             res->Reject(ctx, info[0]).Check();
                                         },
                                         dataArr
                )
                                         .ToLocalChecked();

                promise->Then(context, resolveHandler, rejectHandler).ToLocalChecked();
            }
        } else {
            resolver->Resolve(context, ns).Check();
        }
    } catch (Exception const& e) {
        resolver->Reject(context, ValueHelper::unwrap(e.exception())).Check();
    } catch (std::exception const& e) {
        v8::Local<v8::String> msg;
        if (v8::String::NewFromUtf8(isolate, (std::string("C++ Exception: ") + e.what()).c_str()).ToLocal(&msg)) {
            resolver->Reject(context, v8::Exception::Error(msg)).Check();
        }
    } catch (...) {
        resolver
            ->Reject(
                context,
                v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "Unknown C++ exception occurred"))
            )
            .Check();
    }

    return resolver->GetPromise();
}
void V8ModuleLoader::HostInitializeImportMetaObjectCallback(
    v8::Local<v8::Context> context,
    v8::Local<v8::Module>  module,
    v8::Local<v8::Object>  meta
) {
    auto isolate = context->GetIsolate();

    try {
        auto engine = static_cast<Engine*>(context->GetAlignedPointerFromEmbedderData(kEngineContextEmbedderDataIndex));

        std::string moduleUrl = "";
        int         hash      = module->GetIdentityHash();
        if (auto it = engine->moduleIdentityMap_.find(hash); it != engine->moduleIdentityMap_.end()) {
            moduleUrl = it->second;
        }

        if (!moduleUrl.empty()) {
            v8::Local<v8::String> urlStr;
            if (v8::String::NewFromUtf8(isolate, moduleUrl.c_str(), v8::NewStringType::kNormal, moduleUrl.size())
                    .ToLocal(&urlStr)) {
                meta->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "url"), urlStr).Check();
            }
        }
    } catch (Exception const& e) {
        v8_backend::V8Helper::rethrowToScript(e);
    } catch (std::exception const& e) {
        v8_backend::V8Helper::rethrowToScript(e);
    } catch (...) {
        isolate->ThrowException(
            v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "Unknown C++ exception occurred"))
        );
    }
}
v8::MaybeLocal<v8::Module> V8ModuleLoader::ResolveModuleCallback(
    v8::Local<v8::Context>    context,
    v8::Local<v8::String>     specifier,
    v8::Local<v8::FixedArray> importAttributes,
    v8::Local<v8::Module>     referrer
) {
    auto isolate = context->GetIsolate();
    auto engine  = static_cast<Engine*>(context->GetAlignedPointerFromEmbedderData(kEngineContextEmbedderDataIndex));

    v8::String::Utf8Value specifierUtf8(isolate, specifier);
    std::string           specifierStr(*specifierUtf8, specifierUtf8.length());

    std::string referrerUrl  = "";
    int         referrerHash = referrer->GetIdentityHash();
    if (auto it = engine->moduleIdentityMap_.find(referrerHash); it != engine->moduleIdentityMap_.end()) {
        referrerUrl = it->second;
    }

    try {
        std::string resolvedUrl = normalizePath(referrerUrl, specifierStr);

        // 1. Native (C++) Module
        if (auto meta = engine->getModuleMeta(resolvedUrl)) {
            if (auto it = engine->loadedModules_.find(resolvedUrl); it != engine->loadedModules_.end()) {
                return v8::Local<v8::Module>::New(isolate, it->second);
            }
            auto module = performNewNativeModule(engine, meta);
            engine->loadedModules_.emplace(resolvedUrl, v8::Global<v8::Module>(isolate, module));
            engine->moduleIdentityMap_.emplace(module->GetIdentityHash(), resolvedUrl); // [新增] O(1) 逆向哈希注册
            return module;
        }

        // 2. Analyze file system level Module cache hits
        if (auto it = engine->loadedModules_.find(resolvedUrl); it != engine->loadedModules_.end()) {
            return v8::Local<v8::Module>::New(isolate, it->second);
        }

        // 3. Parse and compile new modules from the disk
        auto module = performLoadFileModule(engine, resolvedUrl);
        if (!module.IsEmpty()) {
            auto localModule = module.ToLocalChecked();
            engine->loadedModules_.emplace(resolvedUrl, v8::Global<v8::Module>(isolate, localModule));
            engine->moduleIdentityMap_.emplace(localModule->GetIdentityHash(), resolvedUrl); // [新增] O(1) 逆向哈希注册
        }
        return module;
    } catch (Exception const& e) {
        v8_backend::V8Helper::rethrowToScript(e);
        return v8::MaybeLocal<v8::Module>();
    } catch (std::exception const& e) {
        v8_backend::V8Helper::rethrowToScript(e);
        return v8::MaybeLocal<v8::Module>();
    } catch (...) {
        isolate->ThrowException(
            v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "Unknown C++ exception occurred"))
        );
        return v8::MaybeLocal<v8::Module>();
    }
}


} // namespace v8_backend


// impl Engine
Local<Value> Engine::evalScript(Local<String> const& code, Local<String> const& source) {
    v8::TryCatch try_catch(isolate_);

    auto v8Code   = ValueHelper::unwrap(code);
    auto v8Source = ValueHelper::unwrap(source);
    auto ctx      = context_.Get(isolate_);

    auto origin = v8::ScriptOrigin(v8Source);
    auto script = v8::Script::Compile(ctx, v8Code, &origin);
    v8_backend::V8Helper::rethrowException(try_catch);

    auto result = script.ToLocalChecked()->Run(ctx);
    v8_backend::V8Helper::rethrowException(try_catch);
    return ValueHelper::wrap<Value>(result.ToLocalChecked());
}
Local<Value> Engine::evalModule(Local<String> const& code, Local<String> const& source) {
    if (hasFlag(flags_, v8_backend::V8InitializeFlags::NoModuleLoader)) {
        throw std::logic_error("evalModule/registerModule is not supported in AddonMode or NoModuleLoader");
    }
    auto         isolate = isolate_;
    v8::TryCatch try_catch(isolate);
    auto         ctx = context_.Get(isolate);

    auto v8Code   = ValueHelper::unwrap(code);
    auto v8Source = ValueHelper::unwrap(source);

    std::string sourceStr;
    {
        v8::String::Utf8Value utf8(isolate, v8Source);
        sourceStr = std::string(*utf8, utf8.length());
    }

    // Convert an absolute path to a URL following the V8 ModuleLoader conventions
    if (sourceStr != "<eval>" && !sourceStr.starts_with("file://")
        && (sourceStr.find(":/") != std::string::npos || sourceStr.starts_with("/"))) {
        std::filesystem::path p = sourceStr;
        std::error_code       err;
        p = std::filesystem::weakly_canonical(p, err);
        if (!err) {
            std::string gen_path = p.generic_string();
            sourceStr            = "file://";
            if (!gen_path.starts_with("/")) sourceStr += "/";
            sourceStr += gen_path;
            v8Source   = v8::String::NewFromUtf8(isolate, sourceStr.c_str()).ToLocalChecked();
        }
    }

    v8::ScriptOrigin           origin(v8Source, 0, 0, false, -1, v8::Local<v8::Value>(), false, false, true);
    v8::ScriptCompiler::Source compilerSource(v8Code, origin);

    v8::Local<v8::Module> module;
    if (!v8::ScriptCompiler::CompileModule(isolate, &compilerSource).ToLocal(&module)) {
        v8_backend::V8Helper::rethrowException(try_catch);
        return {};
    }

    // Cache entry modules for handling circular dependencies
    if (sourceStr != "<eval>") {
        loadedModules_.emplace(sourceStr, v8::Global<v8::Module>(isolate, module));
        moduleIdentityMap_.emplace(module->GetIdentityHash(), sourceStr); // [新增] O(1) 逆向哈希注册
    }

    // Instantiate Module (Connect Dependency Graph)
    if (module->InstantiateModule(ctx, v8_backend::V8ModuleLoader::ResolveModuleCallback).IsNothing()) {
        v8_backend::V8Helper::rethrowException(try_catch);
        return {};
    }

    v8::Local<v8::Value> result;
    if (!module->Evaluate(ctx).ToLocal(&result)) {
        v8_backend::V8Helper::rethrowException(try_catch);
        return {};
    }

    if (result->IsPromise()) {
        auto promise = result.As<v8::Promise>();
        if (promise->State() == v8::Promise::kRejected) {
            isolate->ThrowException(promise->Result());
            v8_backend::V8Helper::rethrowException(try_catch);
            return {};
        }
    }

    return ValueHelper::wrap<Value>(result);
}


void Engine::gc() { isolate_->LowMemoryNotification(); }

Local<Object> Engine::globalThis() const { return ValueHelper::wrap<Object>(context_.Get(isolate_)->Global()); }

void Engine::registerModule(ModuleMeta const& meta) {
    if (hasFlag(flags_, v8_backend::V8InitializeFlags::NoModuleLoader)) {
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

Local<Value> Engine::performRegisterClass(ClassMeta const& meta) {
    auto engine = asEngine();
    if (engine->registeredClasses_.contains(meta.name_)) {
        throw std::logic_error("Class already registered: " + meta.name_);
    }

    v8::TryCatch vtry(isolate_);

    v8::Local<v8::FunctionTemplate> ctor; // js: new T()

    if (meta.hasConstructor()) {
        ctor = newConstructor(meta);
    } else {
        ctor = v8::FunctionTemplate::New(isolate_, nullptr, {}, {}, 0, v8::ConstructorBehavior::kThrow);
        ctor->RemovePrototype();
    }

    auto scriptClassName = String::newString(meta.name_);
    ctor->SetClassName(ValueHelper::unwrap(scriptClassName));
    setToStringTag(ctor, meta.name_, meta.hasConstructor());

    buildStaticMembers(ctor, meta);
    buildInstanceMembers(ctor, meta);

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
        auto baseCtor = iter->second.Get(isolate_);
        ctor->Inherit(baseCtor);
    }

    auto function = ctor->GetFunction(context_.Get(isolate_));
    v8_backend::V8Helper::rethrowException(vtry);

    if (meta.hasConstructor()) {
        if (engine->instanceClassMapping.contains(meta.typeId_)) {
            throw std::logic_error("Type already registered: " + meta.name_);
        }
        engine->instanceClassMapping.emplace(meta.typeId_, &meta);
    }
    engine->registeredClasses_.emplace(meta.name_, &meta);
    classConstructors_.emplace(&meta, v8::Global<v8::FunctionTemplate>{isolate_, ctor});

    return ValueHelper::wrap<Function>(function.ToLocalChecked());
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

    auto v8Object = ValueHelper::unwrap(object);
    setToStringTag(v8Object, meta.name_);

    registeredEnums_.emplace(meta.name_, &meta);
    enumObject_.emplace(&meta, v8::Global<v8::Object>(isolate_, v8Object));
    return object;
}

bool Engine::isInstanceOf(Local<Object> const& obj, ClassMeta const& meta) const {
    auto iter = classConstructors_.find(&meta);
    if (iter == classConstructors_.end()) {
        return false;
    }
    auto ctor = iter->second.Get(isolate_);
    return ctor->HasInstance(ValueHelper::unwrap(obj));
}

Local<Object> Engine::newInstance(ClassMeta const& meta, std::unique_ptr<NativeInstance>&& instance) {
    auto iter = classConstructors_.find(&meta);
    if (iter == classConstructors_.end()) {
        [[unlikely]] throw std::logic_error{
            "The native class " + meta.name_ + " is not registered, so an instance cannot be constructed."
        };
    }

    v8::TryCatch vtry{isolate_};

    auto ctx  = context_.Get(isolate_);
    auto ctor = iter->second.Get(isolate_)->GetFunction(ctx);
    v8_backend::V8Helper::rethrowException(vtry);

    // (symbol, instance)
    auto args = std::vector<v8::Local<v8::Value>>{
        constructorSymbol_.Get(isolate_).As<v8::Value>(),
        v8::External::New(isolate_, instance.release()) // TODO: 潜在的内存泄漏点(当 Constructor 发生异常时)
    };
    auto val = ctor.ToLocalChecked()->NewInstance(ctx, static_cast<int>(args.size()), args.data());
    v8_backend::V8Helper::rethrowException(vtry);

    return ValueHelper::wrap<Object>(val.ToLocalChecked());
}

InstancePayload* Engine::getInstancePayload(Local<Object> const& obj) const {
    auto v8This = ValueHelper::unwrap(obj);
    if (v8This->InternalFieldCount() < (int)InternalFieldSolt::Count) {
        return nullptr;
    }
    auto payload = v8This->GetAlignedPointerFromInternalField(static_cast<int>(InternalFieldSolt::InstancePayload));
    if (!payload) {
        return nullptr;
    }
    return static_cast<InstancePayload*>(payload);
}
bool Engine::trySetReferenceInternal(Local<Object> const& parentObj, Local<Object> const& subObj) {
    auto v8Parent = ValueHelper::unwrap(parentObj);
    auto v8Child  = ValueHelper::unwrap(subObj);
    if (v8Parent.IsEmpty() || v8Child.IsEmpty()) {
        return false;
    }

    constexpr int count = static_cast<int>(InternalFieldSolt::Count);
    if (v8Parent->InternalFieldCount() < count || v8Child->InternalFieldCount() < count) {
        return false; // 非法对象
    }
    v8Child->SetInternalField(static_cast<int>(InternalFieldSolt::ParentClassThisRef), v8Parent);
    return true;
}

namespace v8_backend {

void V8Engine::setToStringTag(v8::Local<v8::FunctionTemplate>& obj, std::string_view name, bool hasConstructor) {
    auto symbol = v8::Symbol::GetToStringTag(isolate_);
    auto v8str =
        v8::String::NewFromUtf8(isolate_, name.data(), v8::NewStringType::kNormal, name.size()).ToLocalChecked();
    auto attr = static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::ReadOnly | v8::PropertyAttribute::DontEnum);

    if (hasConstructor) {
        obj->PrototypeTemplate()->Set(symbol, v8str, attr); // for log(new Foo)
    } else {
        obj->Set(symbol, v8str, attr); // for log(Bar)
    }
}

void V8Engine::setToStringTag(v8::Local<v8::Object>& obj, std::string_view name) {
    auto symbol = v8::Symbol::GetToStringTag(isolate_);
    auto v8str =
        v8::String::NewFromUtf8(isolate_, name.data(), v8::NewStringType::kNormal, name.size()).ToLocalChecked();
    auto attr = static_cast<v8::PropertyAttribute>(v8::PropertyAttribute::ReadOnly | v8::PropertyAttribute::DontEnum);
    obj->DefineOwnProperty(context_.Get(isolate_), symbol, v8str, attr).Check();
}

v8::Local<v8::Object> V8Engine::newExternalPair(void* data, Engine* engine) {
    v8::EscapableHandleScope scope{engine->isolate_};

    auto tpl = engine->externalPairTemplate_.Get(engine->isolate_);
    assert(tpl->InternalFieldCount() == (int)ExternalPairInternalField::Count);

    auto ctx = engine->context_.Get(engine->isolate_);

    auto obj = tpl->NewInstance(ctx).ToLocalChecked();
    obj->SetAlignedPointerInInternalField(static_cast<int>(ExternalPairInternalField::kData), data);
    obj->SetAlignedPointerInInternalField(static_cast<int>(ExternalPairInternalField::kEngine), engine);
    return scope.Escape(obj);
}
std::pair<void*, Engine*> V8Engine::extractExternalPair(v8::Local<v8::Value> val) {
    if (val.IsEmpty() || !val->IsObject()) [[unlikely]] {
        return {nullptr, nullptr};
    }
    v8::Local<v8::Object> obj = val.As<v8::Object>();

    if (obj->InternalFieldCount() < (int)ExternalPairInternalField::Count) [[unlikely]] {
        return {nullptr, nullptr};
    }

    void*   data = obj->GetAlignedPointerFromInternalField((int)ExternalPairInternalField::kData);
    Engine* engine =
        static_cast<Engine*>(obj->GetAlignedPointerFromInternalField((int)ExternalPairInternalField::kEngine));
    return {data, engine};
}
v8::Local<v8::FunctionTemplate> V8Engine::newConstructor(ClassMeta const& meta) {
    v8::TryCatch vtry{isolate_};

    auto externalPair = newExternalPair(const_cast<ClassMeta*>(&meta), asEngine());
    v8_backend::V8Helper::rethrowException(vtry);

    auto ctor = v8::FunctionTemplate::New(
        isolate_,
        [](v8::FunctionCallbackInfo<v8::Value> const& info) {
            auto isolate = info.GetIsolate();
            auto ctx     = isolate->GetCurrentContext();

            auto&& [data, engine] = extractExternalPair(info.Data());
            if (data == nullptr && engine == nullptr) [[unlikely]] {
                isolate->ThrowError("jspp InternalError: Failed to obtain engine context");
                return;
            }

            auto meta = static_cast<ClassMeta*>(data);

            auto& ctor = meta->instanceMeta_.constructor_;

            EngineScope tracker{engine}; // for addon
            try {
                if (!info.IsConstructCall()) {
                    throw Exception{"Native class constructor cannot be called as a function"};
                }

                std::unique_ptr<NativeInstance> instance        = nullptr;
                bool                            constructFromJs = true;
                if (info.Length() == 2 && info[0]->IsSymbol()
                    && info[0]->StrictEquals(engine->constructorSymbol_.Get(engine->isolate_))
                    && info[1]->IsExternal()) {
                    // constructor call from native code
                    auto inst       = info[1].As<v8::External>()->Value();
                    instance        = std::unique_ptr<NativeInstance>{static_cast<NativeInstance*>(inst)};
                    constructFromJs = false;
                } else {
                    // constructor call from JS code
                    auto args = Arguments{std::make_pair(engine->asEngine(), info)};
                    instance  = ctor(args);
                }

                if (instance == nullptr) {
                    if (constructFromJs) {
                        throw Exception{"This native class cannot be constructed."};
                    } else {
                        throw Exception{"This native class cannot be constructed from native code."};
                    }
                }

                auto payload = new InstancePayload{std::move(instance), meta, engine, constructFromJs};
                info.This()->SetAlignedPointerInInternalField(
                    static_cast<int>(InternalFieldSolt::InstancePayload),
                    payload
                );

                if (constructFromJs) {
                    engine->isolate_->AdjustAmountOfExternalAllocatedMemory(
                        static_cast<int64_t>(meta->instanceMeta_.classSize_)
                    );
                }

                // Trampoline Support
                engine->initClassTrampoline(payload, ValueHelper::wrap<Object>(info.This()));

                // manage InstancePayload
                engine->addManagedResource(payload, info.This(), [](void* payload) {
                    auto typed = static_cast<InstancePayload*>(payload);
                    if (typed->isConstructFromJs()) {
                        typed->getEngine()->isolate_->AdjustAmountOfExternalAllocatedMemory(
                            -static_cast<int64_t>(typed->getDefine()->instanceMeta_.classSize_)
                        );
                    }
                    delete typed;
                });
            } catch (Exception const& e) {
                v8_backend::V8Helper::rethrowToScript(e);
            } catch (std::exception const& e) {
                v8_backend::V8Helper::rethrowToScript(e);
            } catch (...) {
                engine->isolate_->ThrowError("Unknown C++ exception occurred");
            }
        },
        externalPair
    );
    ctor->InstanceTemplate()->SetInternalFieldCount(static_cast<int>(InternalFieldSolt::Count));
    return ctor;
}

void V8Engine::buildStaticMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta) {
    auto const& staticMeta = meta.staticMeta_;

    for (auto& property : staticMeta.property_) {
        auto scriptPropertyName = String::newString(property.name_);

        auto externalPair = newExternalPair(const_cast<StaticMemberMeta::Property*>(&property), asEngine());

        auto v8Getter = [](v8::Local<v8::Name>, v8::PropertyCallbackInfo<v8::Value> const& info) {
            auto&& [data, engine] = extractExternalPair(info.Data());
            auto property         = static_cast<StaticMemberMeta::Property*>(data);

            EngineScope tracker{engine}; // for addon
            try {
                auto ret = property->getter_();
                info.GetReturnValue().Set(ValueHelper::unwrap(ret));
            } catch (Exception const& e) {
                v8_backend::V8Helper::rethrowToScript(e);
            } catch (std::exception const& e) {
                v8_backend::V8Helper::rethrowToScript(e);
            } catch (...) {
                info.GetIsolate()->ThrowError("Unknown C++ exception occurred");
            }
        };

        v8::AccessorNameSetterCallback v8Setter = nullptr;
        if (property.setter_) {
            v8Setter = [](v8::Local<v8::Name>, v8::Local<v8::Value> value, v8::PropertyCallbackInfo<void> const& info) {
                auto&& [data, engine] = extractExternalPair(info.Data());
                auto property         = static_cast<StaticMemberMeta::Property*>(data);

                EngineScope tracker{engine}; // for addon
                try {
                    property->setter_(ValueHelper::wrap<Value>(value));
                } catch (Exception const& e) {
                    v8_backend::V8Helper::rethrowToScript(e);
                } catch (std::exception const& e) {
                    v8_backend::V8Helper::rethrowToScript(e);
                } catch (...) {
                    info.GetIsolate()->ThrowError("Unknown C++ exception occurred");
                }
            };
        }

        obj->SetNativeDataProperty(
            ValueHelper::unwrap(scriptPropertyName).As<v8::Name>(),
            std::move(v8Getter),
            std::move(v8Setter),
            externalPair,
            v8_backend::V8Helper::castAttribute(PropertyAttribute::DontDelete)
        );
    }

    for (auto& function : staticMeta.functions_) {
        auto scriptFunctionName = String::newString(function.name_);

        auto externalPair = newExternalPair(const_cast<StaticMemberMeta::Function*>(&function), asEngine());

        auto fn = v8::FunctionTemplate::New(
            isolate_,
            [](v8::FunctionCallbackInfo<v8::Value> const& info) {
                auto&& [data, engine] = extractExternalPair(info.Data());
                auto func             = static_cast<StaticMemberMeta::Function*>(data);

                EngineScope tracker{engine}; // for addon
                try {
                    auto ret = (func->callback_)(Arguments{std::make_pair(EngineScope::currentEngine(), info)});
                    info.GetReturnValue().Set(ValueHelper::unwrap(ret));
                } catch (Exception const& e) {
                    v8_backend::V8Helper::rethrowToScript(e);
                } catch (std::exception const& e) {
                    v8_backend::V8Helper::rethrowToScript(e);
                } catch (...) {
                    info.GetIsolate()->ThrowError("Unknown C++ exception occurred");
                }
            },
            externalPair,
            {},
            0,
            v8::ConstructorBehavior::kThrow
        );
        obj->Set(ValueHelper::unwrap(scriptFunctionName).As<v8::Name>(), fn, v8::PropertyAttribute::DontDelete);
    }
}
void V8Engine::buildInstanceMembers(v8::Local<v8::FunctionTemplate>& obj, ClassMeta const& meta) {
    auto& instanceMeta = meta.instanceMeta_;

    auto prototype = obj->PrototypeTemplate();
    auto signature = v8::Signature::New(isolate_);

    auto ctx = context_.Get(isolate_);
    auto tag = nativeFunctionTag_.Get(isolate_);

    for (auto& method : instanceMeta.methods_) {
        auto scriptMethodName = String::newString(method.name_);

        auto fn = v8::FunctionTemplate::New(
            isolate_,
            [](v8::FunctionCallbackInfo<v8::Value> const& info) {
                auto method  = static_cast<InstanceMemberMeta::Method*>(info.Data().As<v8::External>()->Value());
                auto payload = info.This()->GetAlignedPointerFromInternalField(
                    static_cast<int>(InternalFieldSolt::InstancePayload)
                );

                auto typed  = static_cast<InstancePayload*>(payload);
                auto engine = typed->getEngine();

                EngineScope tracker{engine}; // for addon
                try {
                    auto val = (method->callback_)(*typed, Arguments{std::make_pair(engine, info)});
                    info.GetReturnValue().Set(ValueHelper::unwrap(val));
                } catch (Exception const& e) {
                    v8_backend::V8Helper::rethrowToScript(e);
                } catch (std::exception const& e) {
                    v8_backend::V8Helper::rethrowToScript(e);
                } catch (...) {
                    info.GetIsolate()->ThrowError("Unknown C++ exception occurred");
                }
            },
            v8::External::New(isolate_, const_cast<InstanceMemberMeta::Method*>(&method)),
            signature
        );
        auto jsFunc = fn->GetFunction(ctx).ToLocalChecked();
        jsFunc->SetPrivate(ctx, tag, v8::True(isolate_)); // mark as native function
        prototype->Set(ValueHelper::unwrap(scriptMethodName), fn, v8::PropertyAttribute::DontDelete);
    }

    for (auto& prop : instanceMeta.property_) {
        auto scriptPropertyName = String::newString(prop.name_);
        auto data               = v8::External::New(isolate_, const_cast<InstanceMemberMeta::Property*>(&prop));
        v8::Local<v8::FunctionTemplate> v8Getter;
        v8::Local<v8::FunctionTemplate> v8Setter;

        v8Getter = v8::FunctionTemplate::New(
            isolate_,
            [](v8::FunctionCallbackInfo<v8::Value> const& info) {
                auto prop    = static_cast<InstanceMemberMeta::Property*>(info.Data().As<v8::External>()->Value());
                auto wrapped = info.This()->GetAlignedPointerFromInternalField(
                    static_cast<int>(InternalFieldSolt::InstancePayload)
                );

                auto typed  = static_cast<InstancePayload*>(wrapped);
                auto engine = typed->getEngine();

                EngineScope tracker{engine}; // for addon
                try {
                    auto val = (prop->getter_)(*typed, Arguments{std::make_pair(engine, info)});
                    info.GetReturnValue().Set(ValueHelper::unwrap(val));
                } catch (Exception const& e) {
                    v8_backend::V8Helper::rethrowToScript(e);
                } catch (std::exception const& e) {
                    v8_backend::V8Helper::rethrowToScript(e);
                } catch (...) {
                    info.GetIsolate()->ThrowError("Unknown C++ exception occurred");
                }
            },
            data,
            signature
        );

        if (prop.setter_) {
            v8Setter = v8::FunctionTemplate::New(
                isolate_,
                [](v8::FunctionCallbackInfo<v8::Value> const& info) {
                    auto prop    = static_cast<InstanceMemberMeta::Property*>(info.Data().As<v8::External>()->Value());
                    auto wrapped = info.This()->GetAlignedPointerFromInternalField(
                        static_cast<int>(InternalFieldSolt::InstancePayload)
                    );

                    auto typed  = static_cast<InstancePayload*>(wrapped);
                    auto engine = typed->getEngine();

                    EngineScope tracker{engine}; // for addon
                    try {
                        (prop->setter_)(*typed, Arguments{std::make_pair(engine, info)});
                    } catch (Exception const& e) {
                        v8_backend::V8Helper::rethrowToScript(e);
                    } catch (std::exception const& e) {
                        v8_backend::V8Helper::rethrowToScript(e);
                    } catch (...) {
                        info.GetIsolate()->ThrowError("Unknown C++ exception occurred");
                    }
                },
                data,
                signature
            );
        }

        prototype->SetAccessorProperty(
            ValueHelper::unwrap(scriptPropertyName).As<v8::Name>(),
            v8Getter,
            v8Setter,
            v8::PropertyAttribute::DontDelete
        );
    }
}

} // namespace v8_backend

} // namespace jspp