#define _CRT_SECURE_NO_WARNINGS
#include "scriptc_core.h"

#include "../jspp-backend/BytecodeContainer.h"
#include "quickjs.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace jspp::scriptc {
namespace {

using qjs_backend::nonstd::BytecodeImportEdge;
using qjs_backend::nonstd::ContainerImportInput;
using qjs_backend::nonstd::ContainerModuleInput;
using qjs_backend::nonstd::normalizeModuleName;

// suffix check carved from cutils.h; the header itself is C-only and does not
// compile under C++
bool hasSuffix(char const* str, char const* suffix) {
    auto strLen    = std::strlen(str);
    auto suffixLen = std::strlen(suffix);
    return strLen >= suffixLen && std::strcmp(str + strLen - suffixLen, suffix) == 0;
}

// carved from quickjs-libc's js_load_file; the buffer is NUL-terminated
// (the quickjs lexer reads the sentinel byte at the end of the input)
std::vector<uint8_t> loadFileBytes(std::filesystem::path const& path) {
    std::FILE* f = std::fopen(path.string().c_str(), "rb");
    if (!f) {
        throw std::runtime_error("could not load '" + path.string() + "'");
    }
    std::vector<uint8_t> buffer;
    uint8_t              tmp[8192];
    size_t               n;
    while ((n = std::fread(tmp, 1, sizeof(tmp), f)) > 0) {
        buffer.insert(buffer.end(), tmp, tmp + n);
    }
    bool failed = std::ferror(f) != 0;
    std::fclose(f);
    if (failed) {
        throw std::runtime_error("read error on '" + path.string() + "'");
    }
    buffer.push_back('\0'); // sentinel expected by the quickjs lexer
    return buffer;
}

// minimal error dump; quickjs-libc's js_std_dump_error is not linked here
std::string takeError(JSContext* ctx) {
    std::string message;
    JSValue     e     = JS_GetException(ctx);
    JSValue     msg   = JS_GetPropertyStr(ctx, e, "message");
    JSValue     stack = JS_GetPropertyStr(ctx, e, "stack");
    const char* m     = JS_ToCString(ctx, msg);
    const char* s     = JS_ToCString(ctx, stack);
    message           = (m && *m) ? m : ((s && *s) ? s : "(error)");
    if (s && *s && !(m && *m && std::strstr(s, m))) {
        message += "\n" + std::string{s};
    }
    if (m) JS_FreeCString(ctx, m);
    if (s) JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, msg);
    JS_FreeValue(ctx, e);
    return message;
}

struct CompileState {
    // modules in first-touch order; the entry is prepended last, so the
    // container entry (index 0) is not the first-compiled module
    std::vector<ContainerModuleInput>               modules;
    std::map<std::string, std::uint32_t>            moduleIndex;
    std::vector<ContainerImportInput>               imports;
    std::set<std::pair<std::uint32_t, std::string>> recordedEdges;

    std::filesystem::path    rootDir;
    std::vector<std::string> nativeModules;

    // one entry per relative import seen by normalize during the whole
    // compilation: (importing module name, raw specifier, normalized name)
    std::vector<std::tuple<std::string, std::string, std::string>> pendingNormalize;
};

bool isDeclaredNative(CompileState const& state, std::string_view name) {
    return std::find(state.nativeModules.begin(), state.nativeModules.end(), name) != state.nativeModules.end();
}

/**
 * @brief normalize pass mirroring the runtime resolution chain.
 *
 * Returns the root-relative canonical name so the name baked into each blob
 * (the loader's module_name = the normalize result) matches the container's
 * module table. Relative imports are recorded for the import table; native
 * modules resolve by name like the runtime's getModuleMeta branch and never
 * enter the container.
 */
char* scriptc_normalize(JSContext* ctx, const char* importingModule, const char* importSpecifier, void* opaque) {
    auto* state = static_cast<CompileState*>(opaque);

    try {
        if (isDeclaredNative(*state, importSpecifier)) {
            return js_strdup(ctx, importSpecifier);
        }

        bool relative = importSpecifier[0] == '.'
                     && (importSpecifier[1] == '/' || (importSpecifier[1] == '.' && importSpecifier[2] == '/'));
        if (!relative) {
            throw std::runtime_error(
                "undeclared import specifier '" + std::string{importSpecifier}
                + "' (bare or rooted imports must be declared as native modules via -M)"
            );
        }

        // full path-segment normalization against the importing module's
        // directory: every "." and ".." segment is resolved (not just leading
        // ones), so the same file reached through different spellings lands
        // on one baked name and one table entry
        std::string base = importingModule;
        std::string dir;
        if (auto slash = base.rfind('/'); slash != std::string::npos) {
            dir = base.substr(0, slash);
        }

        std::vector<std::string> parts;
        if (!dir.empty()) {
            std::size_t start = 0;
            while (start != std::string::npos) {
                auto slash = dir.find('/', start);
                auto part  = dir.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
                if (!part.empty() && part != "." && part != "..") {
                    parts.push_back(part);
                }
                start = (slash == std::string::npos) ? std::string::npos : slash + 1;
            }
        }
        std::string rest  = importSpecifier;
        std::size_t start = 0;
        while (start != std::string::npos) {
            auto slash = rest.find('/', start);
            auto part  = rest.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
            bool last  = slash == std::string::npos;
            if (part == "..") {
                if (parts.empty()) {
                    // ".." consumed the last directory element: this import
                    // would leave the root, where only native modules live
                    throw std::runtime_error(
                        "import escapes the compilation root: '" + std::string{importSpecifier} + "' (from '" + base
                        + "'); declare it as a native module via -M"
                    );
                }
                parts.pop_back();
            } else if (!part.empty() && part != ".") {
                parts.push_back(part);
            }
            if (last) {
                break;
            }
            start = slash + 1;
        }
        std::string canonical;
        for (auto const& part : parts) {
            if (!canonical.empty()) {
                canonical += '/';
            }
            canonical += part;
        }

        state->pendingNormalize.emplace_back(base, importSpecifier, canonical);
        return js_strdup(ctx, canonical.c_str());
    } catch (std::exception const& e) {
        JS_ThrowReferenceError(ctx, "%s", e.what());
        return nullptr;
    }
}

// carved from qjsc.c: dummy init for native module stubs; module bodies are
// never evaluated during compile-only compilation, so this must never run
int nativeStubInit(JSContext*, JSModuleDef*) {
    abort();
    return -1; // pacify compilers that warn about the missing return
}

/**
 * @brief Loader: compile dependency modules and collect their blobs.
 *
 * Invoked during the entry's compile-only JS_Eval for every not-yet-registered
 * dependency, in resolution order.
 */
JSModuleDef* scriptc_loader(JSContext* ctx, const char* module_name, void* opaque, JSValueConst /*attr*/) {
    auto* state = static_cast<CompileState*>(opaque);

    try {
        if (isDeclaredNative(*state, module_name)) {
            // stub def: resolves by name here; the runtime resolves the same
            // name through its own registry at deploy time. The stub body
            // never runs (compile-only), see nativeStubInit.
            return JS_NewCModule(ctx, module_name, nativeStubInit);
        }

        if (!hasSuffix(module_name, ".js") && !hasSuffix(module_name, ".mjs")) {
            throw std::runtime_error(
                "import specifier must reference a .js file (or be declared via -M): '" + std::string{module_name} + "'"
            );
        }

        if (state->moduleIndex.contains(module_name)) {
            // quickjs only calls the loader for names missing from
            // ctx->loaded_modules; reaching here twice means a stale table
            throw std::runtime_error("internal error: module compiled twice: " + std::string{module_name});
        }

        auto    path     = state->rootDir / std::filesystem::path{module_name};
        auto    bytes    = loadFileBytes(path);
        JSValue func_val = JS_Eval(
            ctx,
            reinterpret_cast<char const*>(bytes.data()),
            bytes.size() - 1,
            module_name,
            JS_EVAL_TYPE_MODULE | JS_EVAL_FLAG_COMPILE_ONLY
        );
        if (JS_IsException(func_val)) {
            throw std::runtime_error("compile '" + std::string{module_name} + "': " + takeError(ctx));
        }

        size_t   blobSize = 0;
        uint8_t* blob     = JS_WriteObject(ctx, &blobSize, func_val, JS_WRITE_OBJ_BYTECODE);
        // the module def stays alive in ctx->loaded_modules (js_new_module_def
        // registers on creation); drop the local reference afterwards
        JSModuleDef* def = static_cast<JSModuleDef*>(JS_VALUE_GET_PTR(func_val));
        JS_FreeValue(ctx, func_val);
        if (!blob) {
            throw std::runtime_error("serialize '" + std::string{module_name} + "': " + takeError(ctx));
        }
        std::vector<uint8_t> owned(blob, blob + blobSize);
        js_free(ctx, blob);

        state->moduleIndex.emplace(module_name, static_cast<std::uint32_t>(state->modules.size()));
        state->modules.push_back(ContainerModuleInput{module_name, std::move(owned)});
        return def;
    } catch (std::exception const& e) {
        JS_ThrowReferenceError(ctx, "%s", e.what());
        return nullptr;
    }
}

void compileEntry(
    JSContext*         ctx,
    CompileState&      state,
    std::string const& logicalEntryName,
    std::string const& bakedEntryName,
    bool               forceModule
) {
    // the file is read through its logical root-relative path; the baked name
    // (JS_Eval's module name) may differ when the CLI -n override is used
    auto entryPath = state.rootDir / std::filesystem::path{logicalEntryName};
    auto bytes     = loadFileBytes(entryPath);

    int asModule  = forceModule || logicalEntryName.ends_with(".mjs")
                 || JS_DetectModule(reinterpret_cast<char const*>(bytes.data()), bytes.size() - 1);
    int evalFlags = JS_EVAL_FLAG_COMPILE_ONLY | (asModule ? JS_EVAL_TYPE_MODULE : JS_EVAL_TYPE_GLOBAL);

    // The entry compiles under its root-relative baked name so the name inside
    // the blob matches the container's module table entry (and runtime lookups).
    // The baked name doubles as the base for the entry's own relative imports
    // (documented -n semantics), so an override into a different directory
    // shifts those imports accordingly and must resolve under the root.
    JSValue obj =
        JS_Eval(ctx, reinterpret_cast<char const*>(bytes.data()), bytes.size() - 1, bakedEntryName.c_str(), evalFlags);
    if (JS_IsException(obj)) {
        throw std::runtime_error("compile '" + logicalEntryName + "': " + takeError(ctx));
    }

    for (auto const& [importer, specifier, canonical] : state.pendingNormalize) {
        // container index 0 is the entry (not yet inserted into moduleIndex);
        // dependencies shift by one behind their compilation order index
        auto indexOf = [&](std::string const& name) -> std::uint32_t {
            if (name == bakedEntryName) {
                return 0;
            }
            auto it = state.moduleIndex.find(name);
            if (it == state.moduleIndex.end()) {
                throw std::runtime_error("internal error: module not compiled: '" + name + "'");
            }
            return it->second + 1;
        };
        auto importerIdx = indexOf(importer);
        auto targetIdx   = indexOf(canonical);
        auto key         = std::make_pair(importerIdx, specifier);
        if (state.recordedEdges.insert(key).second) {
            state.imports.push_back(ContainerImportInput{importerIdx, targetIdx, specifier});
        }
    }

    size_t   blobSize = 0;
    uint8_t* blob     = JS_WriteObject(ctx, &blobSize, obj, JS_WRITE_OBJ_BYTECODE);
    JS_FreeValue(ctx, obj);
    if (!blob) {
        throw std::runtime_error("serialize '" + logicalEntryName + "': " + takeError(ctx));
    }
    std::vector<uint8_t> owned(blob, blob + blobSize);
    js_free(ctx, blob);

    // entry first in the container (index 0); dependencies keep compilation
    // completion order (an inner dependency finishes its nested imports and is
    // collected before its importer). The physical order carries no meaning
    // for the runtime, which derives its read order from the import table
    state.modules.emplace(state.modules.begin(), ContainerModuleInput{bakedEntryName, std::move(owned)});
}

} // namespace

namespace {

// frees the compile-time runtime/context on scope exit: the guard keeps the
// cleanup on a single path and the success path just marks itself done
struct QuickJsGuard {
    JSRuntime* rt   = nullptr;
    JSContext* ctx  = nullptr;
    bool       done = false;

    ~QuickJsGuard() {
        if (done) {
            return;
        }
        if (ctx) {
            JS_FreeContext(ctx);
        }
        if (rt) {
            JS_FreeRuntime(rt);
        }
    }
};

} // namespace

std::vector<std::uint8_t> compileModuleContainer(
    std::filesystem::path const&      rootDirIn,
    std::string const&                entryName,
    std::vector<std::string> const&   nativeModules,
    bool                              forceModule,
    std::optional<std::string> const& bakedEntryName
) {
    auto rootDir = std::filesystem::weakly_canonical(rootDirIn);

    // entry names obey the same canonical root-relative rule as module table
    // names: absolute paths, drive prefixes and ".." escapes are rejected here
    // instead of being baked into the bytecode. An explicit empty override is
    // rejected by normalizeModuleName rather than silently falling back.
    auto logicalEntry = normalizeModuleName(entryName);
    auto bakeName     = bakedEntryName.has_value() ? normalizeModuleName(*bakedEntryName) : logicalEntry;

    QuickJsGuard guard;
    guard.rt  = JS_NewRuntime();
    guard.ctx = guard.rt ? JS_NewContext(guard.rt) : nullptr;
    if (!guard.rt || !guard.ctx) {
        throw std::runtime_error("could not create quickjs runtime");
    }
    auto* rt  = guard.rt;
    auto* ctx = guard.ctx;

    CompileState state;
    state.rootDir       = rootDir;
    state.nativeModules = nativeModules;

    JS_SetModuleLoaderFunc2(rt, scriptc_normalize, scriptc_loader, nullptr, &state);
    compileEntry(ctx, state, logicalEntry, bakeName, forceModule);

    // cycle check on the finished graph (no serialization order can
    // satisfy a cyclic import table); also validates index bounds
    std::vector<std::string> names;
    names.reserve(state.modules.size());
    for (auto const& module : state.modules) {
        names.push_back(module.name);
    }
    std::vector<BytecodeImportEdge> edges;
    edges.reserve(state.imports.size());
    for (auto const& edge : state.imports) {
        edges.push_back(BytecodeImportEdge{edge.importerIndex, edge.targetIndex, edge.specifier});
    }
    (void)containerReadOrder(names, edges);

    auto bytes = writeBytecodeContainer(state.modules, state.imports);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    guard.done = true;
    return bytes;
}

} // namespace jspp::scriptc
