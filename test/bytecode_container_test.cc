// Bytecode container tests: container format roundtrip and validation, the
// scriptc compile core (positive cases and rejection matrix), and the jspp
// container loading paths (entry evaluation, namespaces, native module
// cooperation, dynamic import edges, failure reporting).
//
// The container format ('JSC1') bundles one quickjs bytecode blob per module
// plus a module table and an import table; see
// src-quickjs/jspp-backend/BytecodeContainer.h. The scriptc compile core is
// linked into this test binary so container fixtures are produced by the same
// quickjs build as the engine under test (bytecode is version-locked).

#ifdef JSPP_BACKEND_QUICKJS

#include "jspp/Jspp.h"
#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/Value.h"

#include "jspp-backend/BytecodeContainer.h"
#include "jspp-backend/queue/JobQueue.h"
#include "scriptc_core.h"


#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <tuple>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {

using namespace jspp;
using namespace jspp::binding;
using namespace jspp::qjs_backend;

// ============================================================================
// Filesystem helpers: each case builds its module tree in an isolated
// temporary directory (process-unique name so parallel test runs cannot
// collide) so cases stay independent
// ============================================================================

std::string currentProcessTag() {
#ifdef _WIN32
    return std::to_string(GetCurrentProcessId());
#else
    return std::to_string(getpid());
#endif
}

// Recompute the outer container checksum after hand-editing bytes: negative
// layout samples must fail on their targeted structural error, not on the
// checksum that covers them.
void refreshChecksum(std::vector<std::uint8_t>& bytes) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 16; i < bytes.size(); i++) {
        hash = (hash ^ bytes[i]) * 1099511628211ULL;
    }
    for (int i = 0; i < 8; i++) {
        bytes[8 + i] = std::uint8_t(hash >> (8 * i));
    }
}

std::vector<std::uint8_t> makeContainerBytes(
    std::vector<nonstd::ContainerModuleInput> const& modules,
    std::vector<nonstd::ContainerImportInput> const& imports
) {
    return nonstd::writeBytecodeContainer(modules, imports);
}

// Hand-craft a container byte layout for negative parser tests: header, one
// module table entry per module (name + start/end), an import table, and raw
// payload bytes behind the tables. Returns bytes with a valid outer checksum;
// individual fields are then patched and the checksum refreshed per case.
std::vector<std::uint8_t> craftContainer(
    std::vector<std::string> const&                                           names,
    std::vector<std::vector<std::uint8_t>> const&                             blobs,
    std::vector<std::tuple<std::uint32_t, std::string, std::uint32_t>> const& edges
) {
    std::vector<std::uint8_t> out;
    auto                      putU32 = [&out](std::uint32_t v) {
        out.push_back(std::uint8_t(v));
        out.push_back(std::uint8_t(v >> 8));
        out.push_back(std::uint8_t(v >> 16));
        out.push_back(std::uint8_t(v >> 24));
    };
    auto putBytes  = [&out](std::vector<std::uint8_t> const& v) { out.insert(out.end(), v.begin(), v.end()); };
    auto putString = [&out](std::string const& v) { out.insert(out.end(), v.begin(), v.end()); };

    out.insert(out.end(), {'J', 'S', 'C', '1'});
    putU32(1);                   // version
    out.insert(out.end(), 8, 0); // checksum placeholder
    putU32(static_cast<std::uint32_t>(names.size()));
    // header size: magic(4) + version(4) + checksum(8) + module_count(4)
    constexpr std::size_t kCraftHeaderSize = 20;

    std::size_t cursor = kCraftHeaderSize;
    for (auto const& name : names) {
        cursor += 4 + name.size() + 4 + 4;
    }
    cursor += 4; // import_count
    for (auto const& [importer, specifier, target] : edges) {
        cursor += 4 + 4 + specifier.size() + 4;
    }

    for (std::size_t i = 0; i < names.size(); i++) {
        putU32(static_cast<std::uint32_t>(names[i].size()));
        putString(names[i]);
        putU32(static_cast<std::uint32_t>(cursor)); // start (patched below)
        cursor += blobs[i].size();
        putU32(static_cast<std::uint32_t>(cursor)); // end
    }
    putU32(static_cast<std::uint32_t>(edges.size()));
    for (auto const& [importer, specifier, target] : edges) {
        putU32(importer);
        putU32(static_cast<std::uint32_t>(specifier.size()));
        putString(specifier);
        putU32(target);
    }
    for (auto const& blob : blobs) {
        putBytes(blob);
    }
    refreshChecksum(out);
    return out;
}

struct ScopedTempDir {
    std::filesystem::path dir;

    explicit ScopedTempDir(std::string_view name)
    : dir(std::filesystem::temp_directory_path() / (std::string{name} + "-" + currentProcessTag())) {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
        std::filesystem::create_directories(dir);
    }

    ~ScopedTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }

    void writeFile(std::string_view filename, std::string_view content) {
        auto path = dir / filename;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.is_open());
        out << content;
        REQUIRE(out.good());
    }

    void writeBinary(std::string_view filename, std::span<std::uint8_t const> bytes) {
        auto path = dir / filename;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path());
        }
        std::ofstream out(path, std::ios::binary);
        REQUIRE(out.is_open());
        out.write(reinterpret_cast<char const*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        REQUIRE(out.good());
    }

    std::string getFileUrl(std::string_view filename) const {
        auto path = (dir / filename).generic_string();
        if (!path.empty() && path[0] != '/') {
            return "file:///" + path;
        }
        return "file://" + path;
    }

    std::vector<std::uint8_t> readBinary(std::string_view filename) const {
        std::ifstream in(dir / filename, std::ios::binary);
        REQUIRE(in.is_open());
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>{in}), std::istreambuf_iterator<char>{});
    }

    std::vector<std::uint8_t> compile(
        std::string const&                entry,
        std::vector<std::string> const&   nativeModules  = {},
        bool                              forceModule    = false,
        std::optional<std::string> const& bakedEntryName = std::nullopt
    ) const {
        return scriptc::compileModuleContainer(dir, entry, nativeModules, forceModule, bakedEntryName);
    }
};

// Three-module graph used by the loading tests:
//   main.js imports ./dep.js (and native 'app_mod' in the native case);
//   dep.js imports ./num.js.
void writeStandardGraph(ScopedTempDir& dir) {
    dir.writeFile("main.js", R"(
        import { describe } from './dep.js';
        export const WHO = describe();
        globalThis.container_result = WHO;
    )");
    dir.writeFile("dep.js", R"(
        import { VALUE } from './num.js';
        export function describe() { return 'dep-' + VALUE; }
    )");
    dir.writeFile("num.js", "export const VALUE = 41;\n");
}

// Path of the scriptc executable next to this test binary (built together
// with JSPP_BUILD_TOOLS=ON); empty when the tools were not built.
std::filesystem::path scriptcExePath() {
#ifdef _WIN32
    char selfBuf[MAX_PATH];
    GetModuleFileNameA(nullptr, selfBuf, MAX_PATH);
    std::filesystem::path self{selfBuf};
#else
    std::filesystem::path self{"/proc/self/exe"};
#endif
    std::error_code ec;
    auto            resolved = std::filesystem::canonical(self, ec);
    if (ec) {
        return {};
    }
    auto tool = resolved.parent_path()
#ifdef _WIN32
              / "scriptc.exe";
#else
              / "scriptc";
#endif
    return std::filesystem::exists(tool) ? tool : std::filesystem::path{};
}

// Run the scriptc CLI with quoted arguments; returns the process exit code.
// When stderrPath is non-empty, the child's stderr is redirected there so
// diagnostics can be asserted.
int runScriptc(std::filesystem::path const& exe, std::string const& args, std::string const& stderrPath = {}) {
    auto redirect = stderrPath.empty() ? std::string{} : (" 2> \"" + stderrPath + "\"");
#ifdef _WIN32
    // the extra outer quote pair protects the command from cmd.exe's leading
    // quote stripping (a command string that starts with a quote loses its
    // first and last quote pair under "cmd /c")
    auto cmd = "\"\"" + exe.string() + "\" " + args + redirect + "\"";
#else
    // plain shell quoting: no cmd.exe rewrite layer in between
    auto cmd = "\"" + exe.string() + "\" " + args + redirect;
#endif
    return std::system(cmd.c_str());
}

struct LoadState {
    std::string lastLog;
    int         invokeCount = 0;
};

static auto ContainerNativeMeta = defModule("app_config").export_constant("LEVEL", 7).build();

std::unique_ptr<Engine> makeEngine() {
    auto engine = std::make_unique<Engine>();
    engine->setData(std::make_shared<LoadState>());
    return engine;
}

} // namespace

// ============================================================================
// Container format layer
// ============================================================================

TEST_CASE("Bytecode container: write and parse roundtrip", "[bytecode-container]") {
    // the edge's specifier matches main.js importing ./lib/dep.js: the fixture
    // stays semantically consistent (the parser stores fields verbatim, it
    // does not re-derive import targets)
    std::vector<nonstd::ContainerModuleInput> modules = {
        {   "main.js", {1, 2, 3, 4, 5}},
        {"lib/dep.js",       {9, 8, 7}},
    };
    std::vector<nonstd::ContainerImportInput> imports = {
        {0, 1, "./lib/dep.js"},
    };

    auto bytes  = nonstd::writeBytecodeContainer(modules, imports);
    auto parsed = nonstd::parseBytecodeContainer(bytes);

    // header: magic 'JSC1' and format version 1
    REQUIRE(bytes.size() >= 12);
    REQUIRE(bytes[0] == 'J');
    REQUIRE(bytes[1] == 'S');
    REQUIRE(bytes[2] == 'C');
    REQUIRE(bytes[3] == '1');
    REQUIRE(nonstd::hasContainerMagic(bytes));

    REQUIRE(parsed.modules.size() == 2);
    REQUIRE(parsed.modules[0].name == "main.js");
    REQUIRE(parsed.modules[1].name == "lib/dep.js");

    // payload bytes survive unchanged and payloadOf points at the right slice
    REQUIRE(
        std::vector<std::uint8_t>(
            parsed.payloadOf(parsed.modules[0]).begin(),
            parsed.payloadOf(parsed.modules[0]).end()
        )
        == modules[0].blob
    );
    REQUIRE(
        std::vector<std::uint8_t>(
            parsed.payloadOf(parsed.modules[1]).begin(),
            parsed.payloadOf(parsed.modules[1]).end()
        )
        == modules[1].blob
    );

    // import table keeps the raw specifier and resolves both indices
    REQUIRE(parsed.imports.size() == 1);
    REQUIRE(parsed.imports[0].specifier == "./lib/dep.js");
    REQUIRE(parsed.imports[0].importerIndex == 0);
    REQUIRE(parsed.imports[0].targetIndex == 1);

    REQUIRE(parsed.findModule("lib/dep.js") == &parsed.modules[1]);
    REQUIRE(parsed.findModule("missing.js") == nullptr);
    REQUIRE(parsed.findImport("main.js", "./lib/dep.js") == &parsed.imports[0]);
    REQUIRE(parsed.findImport("main.js", "./other.js") == nullptr);
    REQUIRE(parsed.findImport("lib/dep.js", "./lib/dep.js") == nullptr); // wrong importer
}

TEST_CASE("Bytecode container: parse rejects malformed input", "[bytecode-container]") {
    std::vector<nonstd::ContainerModuleInput> modules = {
        {"main.js", {1, 2, 3}},
        { "dep.js", {4, 5, 6}},
    };
    std::vector<nonstd::ContainerImportInput> imports = {
        {0, 1, "./dep.js"}
    };
    auto bytes = nonstd::writeBytecodeContainer(modules, imports);

    // corrupt magic: the container tag is the first four bytes
    {
        auto bad = bytes;
        bad[0]   = 'X';
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("magic"));
    }
    // corrupt format version: byte 4..7 hold the version
    {
        auto bad = bytes;
        bad[7]   = 0x7F;
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("version"));
    }
    // corrupt payload: the checksum covers everything from module_count on
    {
        auto bad    = bytes;
        bad.back() ^= 0xFF;
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("checksum"));
    }
    // truncated header: shorter than magic+version+checksum+module_count
    {
        auto bad = bytes;
        bad.resize(10);
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("truncated"));
    }
    // truncated module table: header intact, table cut off
    {
        auto bad = bytes;
        bad.resize(24);
        // recompute the checksum so truncation (not corruption) is exercised
        std::uint64_t hash = 14695981039346656037ULL;
        for (std::size_t i = 16; i < bad.size(); i++) {
            hash = (hash ^ bad[i]) * 1099511628211ULL;
        }
        for (int i = 0; i < 8; i++) {
            bad[8 + i] = std::uint8_t(hash >> (8 * i));
        }
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("truncated"));
    }
    // import edge index out of range: hand-build a valid-checksum container
    // whose only defect is the edge's target index. Header (20) + module table
    // (4 + 7 name + 4 + 4) + import table (4 + 4 + 4 + 8 + 4) puts the payload
    // region at offset 63; the sample sets start=63/end=66 accordingly.
    {
        std::vector<std::uint8_t> crafted;
        auto                      putU32 = [&crafted](std::uint32_t v) {
            crafted.push_back(std::uint8_t(v));
            crafted.push_back(std::uint8_t(v >> 8));
            crafted.push_back(std::uint8_t(v >> 16));
            crafted.push_back(std::uint8_t(v >> 24));
        };
        crafted.insert(crafted.end(), {'J', 'S', 'C', '1'});
        putU32(1);                           // version
        crafted.insert(crafted.end(), 8, 0); // checksum placeholder
        putU32(1);                           // module_count
        putU32(7);                           // name_len "main.js"
        crafted.insert(crafted.end(), {'m', 'a', 'i', 'n', '.', 'j', 's'});
        putU32(63); // start: payload region begins right after the tables
        putU32(66); // end
        putU32(1);  // import_count
        putU32(0);  // importer index
        putU32(8);  // specifier_len "./dep.js"
        crafted.insert(crafted.end(), {'.', '/', 'd', 'e', 'p', '.', 'j', 's'});
        putU32(9);                                // target index: out of range (the only defect)
        crafted.insert(crafted.end(), {1, 2, 3}); // payload bytes at 63..66
        refreshChecksum(crafted);
        REQUIRE_THROWS_WITH(
            nonstd::parseBytecodeContainer(crafted),
            Catch::Matchers::ContainsSubstring("out of range")
        );
    }
    // empty module table is rejected: a container always has an entry
    {
        auto bad = nonstd::writeBytecodeContainer(modules, imports);
        // module_count sits right after the checksum (offset 16)
        bad[16]            = 0;
        std::uint64_t hash = 14695981039346656037ULL;
        for (std::size_t i = 16; i < bad.size(); i++) {
            hash = (hash ^ bad[i]) * 1099511628211ULL;
        }
        for (int i = 0; i < 8; i++) {
            bad[8 + i] = std::uint8_t(hash >> (8 * i));
        }
        REQUIRE_THROWS_WITH(
            nonstd::parseBytecodeContainer(bad),
            Catch::Matchers::ContainsSubstring("empty module table")
        );
    }
}

TEST_CASE("Bytecode container: read order is dependency first", "[bytecode-container]") {
    // linear chain: entry(0) -> a(1) -> b(2); post-order reads b, a, entry
    {
        std::vector<std::string>                names = {"main.js", "a.js", "b.js"};
        std::vector<nonstd::BytecodeImportEdge> edges = {
            {0, 1, "./a.js"},
            {1, 2, "./b.js"}
        };
        auto order = containerReadOrder(names, edges);
        REQUIRE(order == std::vector<std::uint32_t>{2, 1, 0});
    }
    // diamond: entry(0) -> left(1) -> base(3), entry -> right(2) -> base;
    // base must precede both importers, entry stays last
    {
        std::vector<std::string>                names = {"main.js", "left.js", "right.js", "base.js"};
        std::vector<nonstd::BytecodeImportEdge> edges = {
            {0, 1,  "./left.js"},
            {0, 2, "./right.js"},
            {1, 3,  "./base.js"},
            {2, 3,  "./base.js"},
        };
        auto order = containerReadOrder(names, edges);
        REQUIRE(order.back() == 0);
        auto basePos  = std::find(order.begin(), order.end(), 3);
        auto leftPos  = std::find(order.begin(), order.end(), 1);
        auto rightPos = std::find(order.begin(), order.end(), 2);
        REQUIRE(basePos < leftPos);
        REQUIRE(basePos < rightPos);
    }
    // cycle is rejected with a path diagnostic (scriptc already refuses to
    // emit cycles; this is the defensive runtime check). The cycle must be
    // reachable from the entry: unreachable modules take no part in the read
    // order.
    {
        std::vector<std::string>                names = {"main.js", "a.js", "b.js"};
        std::vector<nonstd::BytecodeImportEdge> edges = {
            {0, 1, "./a.js"},
            {1, 2, "./b.js"},
            {2, 1, "./a.js"}
        };
        REQUIRE_THROWS_WITH(containerReadOrder(names, edges), Catch::Matchers::ContainsSubstring("circular import"));
    }
}

// ============================================================================
// scriptc compile core
// ============================================================================

TEST_CASE("Bytecode container: module name normalization", "[bytecode-container]") {
    // relative and plain forms converge on the canonical root-relative name
    REQUIRE(nonstd::normalizeModuleName("a.js") == "a.js");
    REQUIRE(nonstd::normalizeModuleName("./a.js") == "a.js");
    REQUIRE(nonstd::normalizeModuleName("sub/a.js") == "sub/a.js");
    REQUIRE(nonstd::normalizeModuleName("./sub/./a.js") == "sub/a.js");

    // absolute paths, drive prefixes, escapes and separators are rejected
    REQUIRE_THROWS_WITH(nonstd::normalizeModuleName(""), Catch::Matchers::ContainsSubstring("empty"));
    REQUIRE_THROWS_WITH(nonstd::normalizeModuleName("/abs/a.js"), Catch::Matchers::ContainsSubstring("root-relative"));
    REQUIRE_THROWS_WITH(nonstd::normalizeModuleName("C:/a.js"), Catch::Matchers::ContainsSubstring("drive"));
    REQUIRE_THROWS_WITH(nonstd::normalizeModuleName("sub\\a.js"), Catch::Matchers::ContainsSubstring("separators"));
    REQUIRE_THROWS_WITH(nonstd::normalizeModuleName("../out.js"), Catch::Matchers::ContainsSubstring("'..'"));
    REQUIRE_THROWS_WITH(nonstd::normalizeModuleName("sub/../a.js"), Catch::Matchers::ContainsSubstring("'..'"));
    REQUIRE_THROWS_WITH(
        nonstd::normalizeModuleName(std::string{"a\0.js", 5}),
        Catch::Matchers::ContainsSubstring("NUL")
    );
    REQUIRE_THROWS_WITH(nonstd::normalizeModuleName("dir/"), Catch::Matchers::ContainsSubstring("file"));
}

TEST_CASE("Bytecode container: parser rejects illegal payload layouts", "[bytecode-container]") {
    // single-module baseline: header (20) + module table (4+7+8) + import
    // count (4) = 39, payload 39..47... plus the empty import table's count
    // field: header 20 + module entry 19 = 39, import table (count only) 4
    // bytes -> payload region 43..51 (8 bytes), file size 51
    auto base = craftContainer(
        {
            "main.js"
    },
        {{1, 2, 3, 4, 5, 6, 7, 8}},
        {}
    );
    REQUIRE(base.size() == 51);
    auto checkStart = [](std::vector<std::uint8_t>& bytes, std::uint32_t start, std::uint32_t end) {
        // start/end fields sit at offset 20 + 4 + 7 = 31
        bytes[31] = std::uint8_t(start);
        bytes[32] = std::uint8_t(start >> 8);
        bytes[33] = std::uint8_t(start >> 16);
        bytes[34] = std::uint8_t(start >> 24);
        bytes[35] = std::uint8_t(end);
        bytes[36] = std::uint8_t(end >> 8);
        bytes[37] = std::uint8_t(end >> 16);
        bytes[38] = std::uint8_t(end >> 24);
        refreshChecksum(bytes);
    };

    // payload starting inside the header
    {
        auto bad = base;
        checkStart(bad, 8, 16);
        REQUIRE_THROWS_WITH(
            nonstd::parseBytecodeContainer(bad),
            Catch::Matchers::ContainsSubstring("outside the payload region")
        );
    }
    // payload starting inside the module table
    {
        auto bad = base;
        checkStart(bad, 20, 26);
        REQUIRE_THROWS_WITH(
            nonstd::parseBytecodeContainer(bad),
            Catch::Matchers::ContainsSubstring("outside the payload region")
        );
    }
    // payload starting inside the import table
    {
        auto bad = base;
        checkStart(bad, 40, 44);
        REQUIRE_THROWS_WITH(
            nonstd::parseBytecodeContainer(bad),
            Catch::Matchers::ContainsSubstring("outside the payload region")
        );
    }
    // end beyond the file
    {
        auto bad = base;
        checkStart(bad, 43, 100);
        REQUIRE_THROWS_WITH(
            nonstd::parseBytecodeContainer(bad),
            Catch::Matchers::ContainsSubstring("outside the payload region")
        );
    }

    // two-module baseline for overlap/gap/trailing checks: header 20 + module
    // table (4+7+8) + (4+6+8) = 57 + import table (4 + 4+4+8+4) = 81, payloads
    // 81..85 and 85..89
    auto two = craftContainer(
        {
            "main.js",
            "dep.js"
    },
        {{1, 2, 3, 4}, {5, 6, 7, 8}},
        {{0, "./dep.js", 1}}
    );
    REQUIRE(two.size() == 89);
    auto patchSpan =
        [](std::vector<std::uint8_t>& bytes, std::size_t entryOffset, std::uint32_t start, std::uint32_t end) {
            // entryOffset points at the module table entry's name_len field;
            // layout: name_len(4) + name(n) + start(4) + end(4)
            auto nameLen   = std::uint32_t(bytes[entryOffset]) | std::uint32_t(bytes[entryOffset + 1]) << 8
                           | std::uint32_t(bytes[entryOffset + 2]) << 16 | std::uint32_t(bytes[entryOffset + 3]) << 24;
            auto startAt   = entryOffset + 4 + nameLen;
            bytes[startAt] = std::uint8_t(start);
            bytes[startAt + 1] = std::uint8_t(start >> 8);
            bytes[startAt + 2] = std::uint8_t(start >> 16);
            bytes[startAt + 3] = std::uint8_t(start >> 24);
            bytes[startAt + 4] = std::uint8_t(end);
            bytes[startAt + 5] = std::uint8_t(end >> 8);
            bytes[startAt + 6] = std::uint8_t(end >> 16);
            bytes[startAt + 7] = std::uint8_t(end >> 24);
            refreshChecksum(bytes);
        };
    // overlapping sections: the second module's payload starts inside the first
    {
        auto bad = two;
        patchSpan(bad, 39, 83, 87); // second entry at 20+19=39
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("overlapping"));
    }
    // gap between sections
    {
        auto bad = two;
        patchSpan(bad, 39, 86, 89);
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("gap"));
    }
    // trailing bytes no module owns
    {
        auto bad = two;
        patchSpan(bad, 39, 85, 88);
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("trailing bytes"));
    }
}

TEST_CASE("Bytecode container: parser rejects broken tables", "[bytecode-container]") {
    // duplicate module names (two table entries with the same name)
    {
        // hand-built: writer refuses duplicates; header 20 + two name entries
        // (4+7+8 each) = 58 + import table 4 = 62, payloads 62..66 and 66..70
        std::vector<std::uint8_t> crafted;
        auto                      putU32 = [&crafted](std::uint32_t v) {
            crafted.push_back(std::uint8_t(v));
            crafted.push_back(std::uint8_t(v >> 8));
            crafted.push_back(std::uint8_t(v >> 16));
            crafted.push_back(std::uint8_t(v >> 24));
        };
        crafted.insert(crafted.end(), {'J', 'S', 'C', '1'});
        putU32(1);
        crafted.insert(crafted.end(), 8, 0);
        putU32(2);
        for (int i = 0; i < 2; i++) {
            putU32(7);
            crafted.insert(crafted.end(), {'m', 'a', 'i', 'n', '.', 'j', 's'});
            putU32(i == 0 ? 62 : 66);
            putU32(i == 0 ? 66 : 70);
        }
        putU32(0); // no edges: the second entry is unreachable as well
        crafted.insert(crafted.end(), {1, 2, 3, 4});
        crafted.insert(crafted.end(), {5, 6, 7, 8});
        refreshChecksum(crafted);
        REQUIRE_THROWS_WITH(
            nonstd::parseBytecodeContainer(crafted),
            Catch::Matchers::ContainsSubstring("duplicate module name")
        );
    }
    // non-canonical module name ("./main.js" instead of "main.js")
    {
        auto bad = craftContainer(
            {
                "./main.js"
        },
            {{1, 2, 3}},
            {}
        );
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("not canonical"));
    }
    // module name with an escape segment
    {
        auto bad = craftContainer(
            {
                "../out.js"
        },
            {{1, 2, 3}},
            {}
        );
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("'..'"));
    }
    // absolute module name
    {
        auto bad = craftContainer(
            {
                "/main.js"
        },
            {{1, 2, 3}},
            {}
        );
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("root-relative"));
    }
    // import edge with a bare specifier (native imports never enter the tables)
    {
        auto bad = craftContainer(
            {
                {"main.js"},
                { "dep.js"}
        },
            {{1, 2, 3}, {4, 5, 6}},
            {{0, "dep_pkg", 1}}
        );
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("not relative"));
    }
    // the same import key twice: duplicate keys are rejected even when both
    // edges point at the same target
    {
        auto bad = craftContainer(
            {
                {"main.js"},
                { "dep.js"}
        },
            {{1, 2, 3}, {4, 5, 6}},
            {{0, "./dep.js", 1}, {0, "./dep.js", 1}}
        );
        REQUIRE_THROWS_WITH(
            nonstd::parseBytecodeContainer(bad),
            Catch::Matchers::ContainsSubstring("duplicate import key")
        );
    }
    // a module unreachable from the entry: v1 requires the table to hold
    // exactly the entry-reachable graph
    {
        auto bad = craftContainer(
            {
                "main.js",
                "dep.js",
                "orphan.js"
        },
            {{1, 2, 3}, {4, 5, 6}, {7, 8, 9}},
            {{0, "./dep.js", 1}}
        );
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(bad), Catch::Matchers::ContainsSubstring("not reachable"));
    }
    // declared counts far beyond the actual records: refused before allocation
    {
        std::vector<std::uint8_t> crafted;
        auto                      putU32 = [&crafted](std::uint32_t v) {
            crafted.push_back(std::uint8_t(v));
            crafted.push_back(std::uint8_t(v >> 8));
            crafted.push_back(std::uint8_t(v >> 16));
            crafted.push_back(std::uint8_t(v >> 24));
        };
        crafted.insert(crafted.end(), {'J', 'S', 'C', '1'});
        putU32(1);
        crafted.insert(crafted.end(), 8, 0);
        putU32(0xFFFFFF); // module_count: no bytes follow to hold one record
        refreshChecksum(crafted);
        REQUIRE_THROWS_WITH(nonstd::parseBytecodeContainer(crafted), Catch::Matchers::ContainsSubstring("truncated"));
    }
}

TEST_CASE("Bytecode container: writer validates encoding-level input", "[bytecode-container]") {
    std::vector<nonstd::ContainerModuleInput> good = {
        {"main.js", {1, 2, 3}},
        { "dep.js", {4, 5, 6}},
    };
    std::vector<nonstd::ContainerImportInput> goodEdges = {
        {0, 1, "./dep.js"}
    };

    // the writer's accepted input always round-trips
    REQUIRE_NOTHROW(nonstd::parseBytecodeContainer(makeContainerBytes(good, goodEdges)));

    // empty module name / payload / specifier
    REQUIRE_THROWS(
        nonstd::writeBytecodeContainer(
            {
                {"", {1}}
    },
            {}
        )
    );
    REQUIRE_THROWS(
        nonstd::writeBytecodeContainer(
            {
                {"main.js", {}}
    },
            {}
        )
    );
    REQUIRE_THROWS(
        nonstd::writeBytecodeContainer(
            good,
            {
                {0, 1, ""}
    }
        )
    );
    // non-canonical name
    REQUIRE_THROWS(
        nonstd::writeBytecodeContainer(
            {
                {"./main.js", {1}}
    },
            {}
        )
    );
    // bare specifier
    REQUIRE_THROWS(
        nonstd::writeBytecodeContainer(
            good,
            {
                {0, 1, "dep_pkg"}
    }
        )
    );
    // duplicate module names
    REQUIRE_THROWS(
        nonstd::writeBytecodeContainer(
            {
                {"main.js", {1}},
                {"main.js", {2}}
    },
            {}
        )
    );
    // duplicate import key (same key, same target)
    REQUIRE_THROWS(
        nonstd::writeBytecodeContainer(
            good,
            {
                {0, 1, "./dep.js"},
                {0, 1, "./dep.js"}
    }
        )
    );
    // out-of-range indices
    REQUIRE_THROWS(
        nonstd::writeBytecodeContainer(
            good,
            {
                {0, 5, "./dep.js"}
    }
        )
    );
    // note: reachability of the graph is a parser-side check (the writer is
    // the compiler's internal serializer and the compiler feeds only the
    // entry-reachable graph)
}

TEST_CASE("Bytecode container: read order validates its input", "[bytecode-container]") {
    std::vector<std::string> names = {"main.js", "a.js"};

    // edge index out of range: the standalone function must not depend on the
    // parser having validated the tables first
    {
        std::vector<nonstd::BytecodeImportEdge> badEdges = {
            {0, 9, "./a.js"}
        };
        REQUIRE_THROWS_WITH(containerReadOrder(names, badEdges), Catch::Matchers::ContainsSubstring("out of range"));
    }
    {
        std::vector<nonstd::BytecodeImportEdge> badEdges = {
            {9, 0, "./a.js"}
        };
        REQUIRE_THROWS_WITH(containerReadOrder(names, badEdges), Catch::Matchers::ContainsSubstring("out of range"));
    }
    // a self-edge is a one-node cycle
    {
        std::vector<nonstd::BytecodeImportEdge> selfEdge = {
            {0, 0, "./self.js"}
        };
        REQUIRE_THROWS_WITH(containerReadOrder(names, selfEdge), Catch::Matchers::ContainsSubstring("circular import"));
    }
    // a deep chain does not overflow the native stack (iterative DFS)
    {
        constexpr std::uint32_t  kDepth = 20000;
        std::vector<std::string> chain;
        for (std::uint32_t i = 0; i < kDepth; i++) {
            chain.push_back("m" + std::to_string(i) + ".js");
        }
        std::vector<nonstd::BytecodeImportEdge> edges;
        for (std::uint32_t i = 0; i + 1 < kDepth; i++) {
            edges.push_back({i, i + 1, "./next.js"});
        }
        auto order = containerReadOrder(chain, edges);
        REQUIRE(order.size() == kDepth);
        REQUIRE(order.front() == kDepth - 1);
        REQUIRE(order.back() == 0);
    }
}

TEST_CASE("scriptc: compiles a module graph into a container", "[scriptc]") {
    ScopedTempDir dir("jspp_scriptc_positive");
    writeStandardGraph(dir);

    auto bytes = dir.compile("main.js");
    REQUIRE(bytes.size() > 32);
    REQUIRE(nonstd::hasContainerMagic(bytes));

    auto container = nonstd::parseBytecodeContainer(bytes);
    REQUIRE(container.modules.size() == 3);
    // module names are root-relative and match the baked blob names; the
    // physical order follows compilation completion (innermost dependency
    // first), and the read order is derived from the import table instead
    REQUIRE(container.modules[0].name == "main.js");
    REQUIRE(container.findModule("dep.js") != nullptr);
    REQUIRE(container.findModule("num.js") != nullptr);

    // import table records raw specifiers with resolved targets
    REQUIRE(container.imports.size() == 2);
    REQUIRE(container.imports[0].specifier == "./dep.js");
    REQUIRE(container.imports[0].importerIndex == 0);
    REQUIRE(container.imports[0].targetIndex == container.findModule("dep.js") - container.modules.data());
    REQUIRE(container.imports[1].specifier == "./num.js");
    REQUIRE(container.imports[1].importerIndex == container.findModule("dep.js") - container.modules.data());
    REQUIRE(container.imports[1].targetIndex == container.findModule("num.js") - container.modules.data());

    // each payload is a self-validating quickjs blob (BC_VERSION + checksum):
    // the header bytes of every section must differ between modules
    auto p0 = container.payloadOf(container.modules[0]);
    auto p1 = container.payloadOf(container.modules[1]);
    REQUIRE(p0.size() > 8);
    REQUIRE(p1.size() > 8);
    REQUIRE_FALSE(std::equal(p0.begin(), p0.end(), p1.begin(), p1.end()));

    // names normalize across directories: the same file reached through
    // different relative spellings lands on a single table entry
    ScopedTempDir dir2("jspp_scriptc_normalize");
    dir2.writeFile("main.js", R"(
        import { V } from './sub/inner.js';
        import { W } from './sub/../sub/inner2.js';
        export const SUM = V + W;
    )");
    dir2.writeFile("sub/inner.js", "import { W } from './inner2.js';\nexport const V = W + 1;\n");
    dir2.writeFile("sub/inner2.js", "export const W = 10;\n");
    auto container2 = nonstd::parseBytecodeContainer(dir2.compile("main.js"));
    REQUIRE(container2.modules.size() == 3);
    REQUIRE(container2.findModule("sub/inner.js") != nullptr);
    REQUIRE(container2.findModule("sub/inner2.js") != nullptr);
}

TEST_CASE("scriptc: rejects broken module graphs", "[scriptc]") {
    // missing dependency: the import points at a file that does not exist
    {
        ScopedTempDir dir("jspp_scriptc_missing");
        dir.writeFile("main.js", "import { X } from './gone.js';\nexport const Y = X;\n");
        REQUIRE_THROWS_WITH(dir.compile("main.js"), Catch::Matchers::ContainsSubstring("gone.js"));
    }
    // undeclared bare specifier: bare imports must be declared with -M
    {
        ScopedTempDir dir("jspp_scriptc_bare");
        dir.writeFile("main.js", "import { X } from 'some_package';\nexport const Y = X;\n");
        REQUIRE_THROWS_WITH(dir.compile("main.js"), Catch::Matchers::ContainsSubstring("undeclared import specifier"));
    }
    // root escape: a relative import leaving the compilation root must be
    // declared native instead
    {
        ScopedTempDir dir("jspp_scriptc_escape");
        dir.writeFile("main.js", "import { X } from '../outside.js';\nexport const Y = X;\n");
        REQUIRE_THROWS_WITH(dir.compile("main.js"), Catch::Matchers::ContainsSubstring("escapes the compilation root"));
    }
    // syntax error in a dependency: reported with the dependency's name
    {
        ScopedTempDir dir("jspp_scriptc_syntax");
        dir.writeFile("main.js", "import { X } from './broken.js';\nexport const Y = X;\n");
        dir.writeFile("broken.js", "export const X = ;\n");
        REQUIRE_THROWS_WITH(dir.compile("main.js"), Catch::Matchers::ContainsSubstring("broken.js"));
    }
    // circular import: no serialization order satisfies every edge
    {
        ScopedTempDir dir("jspp_scriptc_cycle");
        dir.writeFile("main.js", "import { A } from './a.js';\nexport const M = A;\n");
        dir.writeFile("a.js", "import { B } from './b.js';\nexport const A = B + 1;\n");
        dir.writeFile("b.js", "import { A } from './a.js';\nexport const B = A + 1;\n");
        REQUIRE_THROWS_WITH(dir.compile("main.js"), Catch::Matchers::ContainsSubstring("circular import"));
    }
    // missing entry file
    {
        ScopedTempDir dir("jspp_scriptc_noentry");
        dir.writeFile("other.js", "export const V = 1;\n");
        REQUIRE_THROWS_WITH(dir.compile("nope.js"), Catch::Matchers::ContainsSubstring("could not load"));
    }
}

TEST_CASE("scriptc: validates the entry name", "[scriptc]") {
    ScopedTempDir dir("jspp_scriptc_entry");
    dir.writeFile("main.js", "export const V = 1;\n");
    dir.writeFile("sub/inner.js", "export const W = 2;\n");

    // absolute entry paths and escapes are rejected before any file is read.
    // The native-separator form hits the separator check on Windows and the
    // root-relative check on POSIX; both rejections name the offending path.
    auto absEntry = (dir.dir / "main.js").string();
    REQUIRE_THROWS_WITH(
        scriptc::compileModuleContainer(dir.dir, absEntry, {}, false),
        Catch::Matchers::ContainsSubstring("module name must")
    );
#ifdef _WIN32
    auto absEntrySlashes = (dir.dir / "main.js").generic_string();
    REQUIRE_THROWS_WITH(
        scriptc::compileModuleContainer(dir.dir, absEntrySlashes, {}, false),
        Catch::Matchers::ContainsSubstring("drive")
    );
#endif
    REQUIRE_THROWS_WITH(
        scriptc::compileModuleContainer(dir.dir, "../outside.js", {}, false),
        Catch::Matchers::ContainsSubstring("'..'")
    );

    // a relative spelling of a legal entry normalizes to the canonical name
    {
        auto container = nonstd::parseBytecodeContainer(dir.compile("./main.js"));
        REQUIRE(container.modules[0].name == "main.js");
    }
    // subdirectory entries bake their root-relative path
    {
        auto container = nonstd::parseBytecodeContainer(dir.compile("sub/inner.js"));
        REQUIRE(container.modules.size() == 1);
        REQUIRE(container.modules[0].name == "sub/inner.js");
    }
    // the same sources compiled under two different roots produce identical
    // module name sets: names never contain the compilation root's path
    {
        ScopedTempDir dir2("jspp_scriptc_entry_root2");
        dir2.writeFile("main.js", "import { W } from './sub/inner.js';\nexport const V = W;\n");
        dir2.writeFile("sub/inner.js", "export const W = 2;\n");

        dir.writeFile("sub/inner.js", "export const W = 2;\n");
        dir.writeFile("main.js", "import { W } from './sub/inner.js';\nexport const V = W;\n");

        auto first  = nonstd::parseBytecodeContainer(dir.compile("main.js"));
        auto second = nonstd::parseBytecodeContainer(dir2.compile("main.js"));
        REQUIRE(first.modules.size() == second.modules.size());
        for (std::size_t i = 0; i < first.modules.size(); i++) {
            REQUIRE(first.modules[i].name == second.modules[i].name);
        }
        for (auto const& module : first.modules) {
            // the name must not leak the root path of either compilation
            REQUIRE(module.name.find(dir.dir.string()) == std::string::npos);
            REQUIRE(module.name.find(dir2.dir.string()) == std::string::npos);
        }
    }
    // the baked entry name can be overridden (-n), the source path stays
    {
        auto container = nonstd::parseBytecodeContainer(dir.compile("main.js", {}, false, "renamed.js"));
        REQUIRE(container.modules[0].name == "renamed.js");
    }
    // -n defines the entry's logical identity: the baked name is also the base
    // for the entry's own relative imports. Overriding into a directory whose
    // dependencies do not exist there fails explicitly (no silent re-pointing
    // of imports back to the physical location).
    {
        ScopedTempDir dirN("jspp_scriptc_entry_alias");
        dirN.writeFile("main.js", "import { W } from './dep.js';\nexport const V = W;\n");
        dirN.writeFile("dep.js", "export const W = 2;\n");

        REQUIRE_THROWS_WITH(
            scriptc::compileModuleContainer(dirN.dir, "main.js", {}, false, std::string{"alias/main.js"}),
            Catch::Matchers::ContainsSubstring("alias/dep.js")
        );
        // providing the dependency under the override's directory compiles, and
        // both names follow the override's identity
        dirN.writeFile("alias/dep.js", "export const W = 5;\n");
        auto container = nonstd::parseBytecodeContainer(
            scriptc::compileModuleContainer(dirN.dir, "main.js", {}, false, std::string{"alias/main.js"})
        );
        REQUIRE(container.modules.size() == 2);
        REQUIRE(container.modules[0].name == "alias/main.js");
        REQUIRE(container.findModule("alias/dep.js") != nullptr);
        REQUIRE(container.imports[0].specifier == "./dep.js");
        // the physical file outside alias/ was never baked
        REQUIRE(container.findModule("dep.js") == nullptr);
    }
    // an empty -n override is rejected, not silently defaulted
    REQUIRE_THROWS_WITH(
        scriptc::compileModuleContainer(dir.dir, "main.js", {}, false, std::string{""}),
        Catch::Matchers::ContainsSubstring("empty")
    );
}

TEST_CASE("scriptc: native modules stay outside the container", "[scriptc]") {
    ScopedTempDir dir("jspp_scriptc_native");
    dir.writeFile("main.js", R"(
        import { LEVEL } from 'app_config';
        import { V } from './dep.js';
        export const SUM = LEVEL + V;
    )");
    dir.writeFile("dep.js", "export const V = 1;\n");

    // undeclared native import is rejected
    REQUIRE_THROWS(dir.compile("main.js"));

    // declared via -M: recorded by name only, no table entry, no blob
    auto container = nonstd::parseBytecodeContainer(dir.compile("main.js", {"app_config"}));
    REQUIRE(container.modules.size() == 2);
    REQUIRE(container.modules[0].name == "main.js");
    REQUIRE(container.modules[1].name == "dep.js");
    REQUIRE(container.imports.size() == 1);
    REQUIRE(container.imports[0].specifier == "./dep.js");
    REQUIRE(container.findModule("app_config") == nullptr);
}

// ============================================================================
// jspp loading paths
// ============================================================================

TEST_CASE("Engine: loadByteCodeContainer evaluates entry with imports", "[bytecode-container]") {
    ScopedTempDir dir("jspp_container_load");
    writeStandardGraph(dir);
    dir.writeBinary("app.jsc", dir.compile("main.js"));

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};

    REQUIRE_NOTHROW(engine->loadByteCodeContainer(dir.dir / "app.jsc"));

    // entry top-level code ran and read through the import chain
    auto result = engine->globalThis().get(String::newString("container_result"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "dep-41");
}

TEST_CASE("Engine: container entry sees import.meta.main and url", "[bytecode-container]") {
    ScopedTempDir dir("jspp_container_meta");
    dir.writeFile("main.js", "globalThis.metaMain = import.meta.main;\nglobalThis.metaUrl = import.meta.url;\n");
    dir.writeBinary("app.jsc", dir.compile("main.js"));

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};

    REQUIRE_NOTHROW(engine->loadByteCodeContainer(dir.dir / "app.jsc"));

    auto main = engine->globalThis().get(String::newString("metaMain"));
    REQUIRE(main.isBoolean());
    REQUIRE(main.asBoolean().getValue() == true);

    // url is a file:// URL of the container file, not of any source file
    auto url = engine->globalThis().get(String::newString("metaUrl")).asString().getValue();
    REQUIRE(url.starts_with("file://"));
    REQUIRE(url.find("app.jsc") != std::string::npos);
}

TEST_CASE("Engine: loadByteCodeContainerNamespace exposes entry exports", "[bytecode-container]") {
    ScopedTempDir dir("jspp_container_ns");
    dir.writeFile("main.js", R"(
        import { VALUE } from './num.js';
        export const DOUBLED = VALUE * 2;
        export default 'entry-default';
    )");
    dir.writeFile("num.js", "export const VALUE = 21;\n");
    dir.writeBinary("app.jsc", dir.compile("main.js"));

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};

    auto ns = engine->loadByteCodeContainerNamespace(dir.dir / "app.jsc");
    REQUIRE(ns.get(String::newString("DOUBLED")).asNumber().getInt32() == 42);
    REQUIRE(ns.get(String::newString("default")).asString().getValue() == "entry-default");
}

TEST_CASE("Engine: container modules import native modules", "[bytecode-container]") {
    ScopedTempDir dir("jspp_container_native");
    dir.writeFile("main.js", R"(
        import { LEVEL } from 'app_config';
        import { VALUE } from './num.js';
        export const SUM = LEVEL + VALUE;
        globalThis.native_sum = SUM;
    )");
    dir.writeFile("num.js", "export const VALUE = 35;\n");
    dir.writeBinary("app.jsc", dir.compile("main.js", {"app_config"}));

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};
    engine->registerModule(ContainerNativeMeta);

    REQUIRE_NOTHROW(engine->loadByteCodeContainer(dir.dir / "app.jsc"));
    REQUIRE(engine->globalThis().get(String::newString("native_sum")).asNumber().getInt32() == 42);
}

TEST_CASE("Engine: container dynamic import follows static edges only", "[bytecode-container]") {
    ScopedTempDir dir("jspp_container_dynamic");
    // third.js exists on disk but is never statically imported: the runtime
    // must refuse it instead of silently loading it from the file system
    dir.writeFile("main.js", R"(
        import { VALUE } from './num.js';
        import('./num.js').then(m => {
            globalThis.dynamic_ok = m.VALUE;
        }, err => {
            globalThis.dynamic_ok = 'rejected: ' + err;
        });
        import('./absent.js').then(m => {
            globalThis.dynamic_missing = 'loaded: ' + m;
        }, err => {
            globalThis.dynamic_missing = String(err);
        });
        import('./third.js').then(m => {
            globalThis.dynamic_ondisk = 'loaded: ' + m.TAG;
        }, err => {
            globalThis.dynamic_ondisk = String(err);
        });
    )");
    dir.writeFile("num.js", "export const VALUE = 5;\n");
    dir.writeFile("third.js", "export const TAG = 'from-disk';\n");
    dir.writeBinary("app.jsc", dir.compile("main.js"));

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};

    REQUIRE_NOTHROW(engine->loadByteCodeContainer(dir.dir / "app.jsc"));

    // dynamic import resolution continues in the job queue; drain it before
    // reading the recorded results
    engine->getJobQueue()->shutdown(true);
    engine->getJobQueue()->loopAndWait();

    // dynamic import of a statically declared specifier resolves through the
    // import table
    auto ok = engine->globalThis().get(String::newString("dynamic_ok"));
    REQUIRE(ok.isNumber());
    REQUIRE(ok.asNumber().getInt32() == 5);

    // dynamic import without a static edge is an explicit error: it must not
    // fall back to the file system, not even when the file exists on disk
    auto missing = engine->globalThis().get(String::newString("dynamic_missing"));
    REQUIRE(missing.isString());
    REQUIRE_THAT(missing.asString().getValue(), Catch::Matchers::ContainsSubstring("not found in bytecode container"));

    auto onDisk = engine->globalThis().get(String::newString("dynamic_ondisk"));
    REQUIRE(onDisk.isString());
    REQUIRE_THAT(onDisk.asString().getValue(), Catch::Matchers::ContainsSubstring("not found in bytecode container"));
}

TEST_CASE("Engine: container load failures are reported with detail", "[bytecode-container]") {
    ScopedTempDir dir("jspp_container_fail");
    dir.writeFile("main.js", "export const V = 1;\n");
    dir.writeBinary("good.jsc", dir.compile("main.js"));

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};

    // happy path registers the container; a second load of the same container
    // would duplicate module names (documented undefined behavior), so this
    // case only asserts the single load result value
    auto result = engine->loadByteCodeContainer(dir.dir / "good.jsc");
    REQUIRE(result.isObject()); // module evaluation returns a promise

    // corrupt checksum is rejected with the container error, not a quickjs
    // internal blob error
    {
        auto bytes    = dir.readBinary("good.jsc");
        bytes.back() ^= 0xFF;
        dir.writeBinary("corrupt.jsc", bytes);

        auto        engine2 = makeEngine();
        EngineScope scope2{engine2.get()};
        REQUIRE_THROWS_WITH(
            engine2->loadByteCodeContainer(dir.dir / "corrupt.jsc"),
            Catch::Matchers::ContainsSubstring("checksum mismatch")
        );
    }
    // cyclic import graph (hand-built, bypassing scriptc's compile-time
    // refusal) is caught by the defensive read-order check
    {
        // two standalone one-module containers are valid; build a cyclic
        // table by hand instead: a imports b, b imports a
        std::vector<std::uint8_t>                 aBlob(16, 0xAB);
        std::vector<std::uint8_t>                 bBlob(16, 0xCD);
        std::vector<nonstd::ContainerModuleInput> modules = {
            {"a.js", aBlob},
            {"b.js", bBlob}
        };
        std::vector<nonstd::ContainerImportInput> edges = {
            {0, 1, "./b.js"},
            {1, 0, "./a.js"}
        };
        auto cyclic = nonstd::writeBytecodeContainer(modules, edges);
        dir.writeBinary("cycle.jsc", cyclic);

        auto        engine3 = makeEngine();
        EngineScope scope3{engine3.get()};
        REQUIRE_THROWS_WITH(
            engine3->loadByteCodeContainer(dir.dir / "cycle.jsc"),
            Catch::Matchers::ContainsSubstring("circular import")
        );
    }
    // a container whose entry is not a module (script bytecode) is rejected
    // by the namespace accessor. JS_DetectModule classifies compilable input
    // as a module, so script entries only arise from hand-built containers.
    {
        auto ctx  = engine->context();
        auto code = std::string{"globalThis.scriptRan = true;\n"};
        auto value =
            JS_Eval(ctx, code.c_str(), code.size(), "plain.js", JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
        REQUIRE_FALSE(JS_IsException(value));
        size_t   size = 0;
        uint8_t* data = JS_WriteObject(ctx, &size, value, JS_WRITE_OBJ_BYTECODE);
        JS_FreeValue(ctx, value);
        REQUIRE(data != nullptr);
        std::vector<nonstd::ContainerModuleInput> scriptModules = {
            {"plain.js", {data, data + size}}
        };
        js_free(ctx, data);
        dir.writeBinary("script.jsc", nonstd::writeBytecodeContainer(scriptModules, {}));

        auto        engine4 = makeEngine();
        EngineScope scope4{engine4.get()};
        REQUIRE_THROWS_WITH(
            engine4->loadByteCodeContainerNamespace(dir.dir / "script.jsc"),
            Catch::Matchers::ContainsSubstring("not a module")
        );
        // a container load occupies the engine even when the namespace accessor
        // rejects the (non-module) entry: a second load is rejected
        REQUIRE_THROWS_WITH(
            engine4->loadByteCodeContainer(dir.dir / "script.jsc"),
            Catch::Matchers::ContainsSubstring("already loaded")
        );
        // a fresh engine runs the script entry through the plain load
        auto        engine6 = makeEngine();
        EngineScope scope6{engine6.get()};
        REQUIRE_NOTHROW(engine6->loadByteCodeContainer(dir.dir / "script.jsc"));
        REQUIRE(engine6->globalThis().get(String::newString("scriptRan")).asBoolean().getValue() == true);
    }
    // truncated file
    {
        auto bytes = dir.readBinary("good.jsc");
        bytes.resize(6);
        dir.writeBinary("truncated.jsc", bytes);
        auto        engine5 = makeEngine();
        EngineScope scope5{engine5.get()};
        REQUIRE_THROWS_AS(engine5->loadByteCodeContainer(dir.dir / "truncated.jsc"), jspp::Exception);
    }
}

TEST_CASE("Engine: a second container load is rejected", "[bytecode-container]") {
    // An engine holds one container: scriptc bundles an entry and its whole
    // dependency closure per container, so a deployed project maps to exactly
    // one container and one lifecycle. Loading another container, or the same
    // one twice, is rejected before anything is deserialized.
    ScopedTempDir dir("jspp_container_multi");
    dir.writeFile("first/first-main.js", R"(
        import { TAG } from './first-tag.js';
        globalThis.first_entry = TAG;
    )");
    dir.writeFile("first/first-tag.js", "export const TAG = 'first-tag';\n");
    dir.writeFile("second/second-main.js", R"(
        import { TAG } from './second-tag.js';
        globalThis.second_entry = TAG;
    )");
    dir.writeFile("second/second-tag.js", "export const TAG = 'second-tag';\n");

    // each container compiles from its own sub-root
    auto firstBytes  = scriptc::compileModuleContainer(dir.dir / "first", "first-main.js", {}, false);
    auto secondBytes = scriptc::compileModuleContainer(dir.dir / "second", "second-main.js", {}, false);
    dir.writeBinary("first.jsc", firstBytes);
    dir.writeBinary("second.jsc", secondBytes);

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};

    REQUIRE_NOTHROW(engine->loadByteCodeContainer(dir.dir / "first.jsc"));
    REQUIRE(engine->globalThis().get(String::newString("first_entry")).asString().getValue() == "first-tag");

    // another container is rejected; the error names the loaded one
    REQUIRE_THROWS_WITH(
        engine->loadByteCodeContainer(dir.dir / "second.jsc"),
        Catch::Matchers::ContainsSubstring("already loaded") &&
            Catch::Matchers::ContainsSubstring("first.jsc")
    );
    // reloading the same container is rejected the same way
    REQUIRE_THROWS_WITH(
        engine->loadByteCodeContainer(dir.dir / "first.jsc"),
        Catch::Matchers::ContainsSubstring("already loaded")
    );
    // the rejected loads left no trace: the first container still resolves
    // and the engine runs plain scripts
    REQUIRE_NOTHROW(engine->evalScript(String::newString("globalThis.after_reject = 1;")));
    REQUIRE(engine->globalThis().get(String::newString("after_reject")).asNumber().getInt32() == 1);
}

TEST_CASE(
    "Engine: container entry with top-level await is rejected by the namespace accessor",
    "[bytecode-container]"
) {
    // side effects before the first await are real: the flag must be set when
    // the rejection fires (evaluation ran up to the await)
    ScopedTempDir dir("jspp_container_tla");
    dir.writeFile(
        "main.js",
        "globalThis.tla_before_await = true;\nconst v = await Promise.resolve(1);\nexport const V = v;\n"
    );
    dir.writeBinary("app.jsc", dir.compile("main.js"));

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};

    REQUIRE_THROWS_WITH(
        engine->loadByteCodeContainerNamespace(dir.dir / "app.jsc"),
        Catch::Matchers::ContainsSubstring("top-level await")
    );
    // the pre-await side effect happened; the engine stays usable
    REQUIRE(engine->globalThis().get(String::newString("tla_before_await")).asBoolean().getValue() == true);
    REQUIRE_NOTHROW(engine->evalScript(String::newString("globalThis.after_tla = 1;")));
    REQUIRE(engine->globalThis().get(String::newString("after_tla")).asNumber().getInt32() == 1);

    // a second TLA container fails the same way on its own engine (each
    // failure releases its own evaluation promise). The first engine already
    // holds a container, so a second load there would be rejected before
    // reaching the TLA check.
    ScopedTempDir dir2("jspp_container_tla2");
    dir2.writeFile(
        "second-main.js",
        "globalThis.tla2_before = true;\nconst w = await Promise.resolve(2);\nexport const W = w;\n"
    );
    dir2.writeBinary("app2.jsc", dir2.compile("second-main.js"));
    auto        engine2 = makeEngine();
    EngineScope scope2{engine2.get()};
    REQUIRE_THROWS_WITH(
        engine2->loadByteCodeContainerNamespace(dir2.dir / "app2.jsc"),
        Catch::Matchers::ContainsSubstring("top-level await")
    );
    REQUIRE(engine2->globalThis().get(String::newString("tla2_before")).asBoolean().getValue() == true);
    REQUIRE_NOTHROW(engine2->evalScript(String::newString("globalThis.after_tla2 = 2;")));
    REQUIRE(engine2->globalThis().get(String::newString("after_tla2")).asNumber().getInt32() == 2);
}

// ============================================================================
// scriptc CLI integration (drives the real executable; skipped when the
// tools were not built alongside the tests)
// ============================================================================

TEST_CASE("scriptc CLI: argument handling and entry naming", "[scriptc][cli]") {
    auto exe = scriptcExePath();
    if (exe.empty()) {
        INFO("scriptc not built (JSPP_BUILD_TOOLS=OFF); skipping CLI tests");
        return;
    }
    ScopedTempDir dir("jspp_scriptc_cli_args");
    dir.writeFile("main.js", "import { V } from './dep.js';\nexport const S = V;\n");
    dir.writeFile("dep.js", "export const V = 3;\n");

    // missing -o and missing input are usage errors
    REQUIRE(runScriptc(exe, "-o out.jsc") != 0);
    REQUIRE(runScriptc(exe, "main.js") != 0);
    // two input files are rejected
    REQUIRE(runScriptc(exe, "-o out.jsc main.js dep.js") != 0);
    // missing option values are rejected
    REQUIRE(runScriptc(exe, "-o") != 0);
    REQUIRE(runScriptc(exe, "-M") != 0);

    // a successful run writes the container; the CLI accepts absolute input
    // paths (its normal build-system usage), and the module names inside stay
    // root-relative with no trace of the compilation root
    auto outPath = (dir.dir / "out.jsc").string();
    REQUIRE(runScriptc(exe, "-o \"" + outPath + "\" \"" + (dir.dir / "main.js").string() + "\"") == 0);
    {
        auto bytes   = dir.readBinary("out.jsc");
        auto parsed  = nonstd::parseBytecodeContainer(bytes);
        auto rootStr = dir.dir.string();
        REQUIRE(parsed.modules.size() == 2);
        for (auto const& module : parsed.modules) {
            REQUIRE(module.name.find(rootStr) == std::string::npos);
            bool named =
                module.name.find("main.js") != std::string::npos || module.name.find("dep.js") != std::string::npos;
            REQUIRE(named);
        }
    }
    // -n renames the entry's baked module
    {
        auto out2 = (dir.dir / "renamed.jsc").string();
        REQUIRE(runScriptc(exe, "-n entry.js -o \"" + out2 + "\" \"" + (dir.dir / "main.js").string() + "\"") == 0);
        auto parsed = nonstd::parseBytecodeContainer(dir.readBinary("renamed.jsc"));
        REQUIRE(parsed.modules[0].name == "entry.js");
    }
    // an explicit empty -n is rejected instead of falling back to the default
    REQUIRE(
        runScriptc(
            exe,
            "-n \"\" -o \"" + (dir.dir / "empty.jsc").string() + "\" \"" + (dir.dir / "main.js").string() + "\""
        )
        != 0
    );
    REQUIRE_FALSE(std::filesystem::exists(dir.dir / "empty.jsc"));
    // a successful compile emits no internal trace output on stderr
    {
        auto errFile  = (dir.dir / "stderr.txt").string();
        auto outTrace = (dir.dir / "traced.jsc").string();
        REQUIRE(runScriptc(exe, "-o \"" + outTrace + "\" \"" + (dir.dir / "main.js").string() + "\"", errFile) == 0);
        std::ifstream errIn(errFile, std::ios::binary);
        std::string   errContent{(std::istreambuf_iterator<char>{errIn}), std::istreambuf_iterator<char>{}};
        REQUIRE(errContent.find("[trace") == std::string::npos);
    }
}

TEST_CASE("scriptc CLI: a failed compile leaves the previous output intact", "[scriptc][cli]") {
    auto exe = scriptcExePath();
    if (exe.empty()) {
        INFO("scriptc not built (JSPP_BUILD_TOOLS=OFF); skipping CLI tests");
        return;
    }
    ScopedTempDir dir("jspp_scriptc_cli_guard");
    dir.writeFile("good.js", "export const V = 1;\n");
    auto outPath = (dir.dir / "out.jsc").string();

    // produce a valid previous output
    REQUIRE(runScriptc(exe, "-o \"" + outPath + "\" \"" + (dir.dir / "good.js").string() + "\"") == 0);
    auto previous = dir.readBinary("out.jsc");
    REQUIRE(previous.size() > 32);

    // a compile failure (syntax error) must not delete or truncate the old
    // artifact: the compilation finishes before the output file is opened
    dir.writeFile("broken.js", "export const X = ;\n");
    REQUIRE(runScriptc(exe, "-o \"" + outPath + "\" \"" + (dir.dir / "broken.js").string() + "\"") != 0);
    REQUIRE(dir.readBinary("out.jsc") == previous);

    // same guarantee for a missing dependency
    dir.writeFile("missing.js", "import { X } from './gone.js';\nexport const Y = X;\n");
    REQUIRE(runScriptc(exe, "-o \"" + outPath + "\" \"" + (dir.dir / "missing.js").string() + "\"") != 0);
    REQUIRE(dir.readBinary("out.jsc") == previous);

    // an unwritable output path fails without destroying the old file: the
    // fopen on a directory target fails before any byte is written
    std::filesystem::create_directories(dir.dir / "dir.jsc");
    REQUIRE(
        runScriptc(exe, "-o \"" + (dir.dir / "dir.jsc").string() + "\" \"" + (dir.dir / "good.js").string() + "\"") != 0
    );

    // the good output still loads after all the failed runs
    auto        engine = makeEngine();
    EngineScope scope{engine.get()};
    REQUIRE_NOTHROW(engine->loadByteCodeContainer(dir.dir / "out.jsc"));
}

TEST_CASE("scriptc CLI: produced container loads directly in jspp", "[scriptc][cli]") {
    auto exe = scriptcExePath();
    if (exe.empty()) {
        INFO("scriptc not built (JSPP_BUILD_TOOLS=OFF); skipping CLI tests");
        return;
    }
    ScopedTempDir dir("jspp_scriptc_cli_e2e");
    writeStandardGraph(dir);
    auto outPath = (dir.dir / "app.jsc").string();
    REQUIRE(runScriptc(exe, "-o \"" + outPath + "\" \"" + (dir.dir / "main.js").string() + "\"") == 0);

    // full CLI-to-runtime chain: the file the tool wrote is loaded without
    // any in-process recompilation
    {
        auto        engine = makeEngine();
        EngineScope scope{engine.get()};
        REQUIRE_NOTHROW(engine->loadByteCodeContainer(dir.dir / "app.jsc"));
        auto result = engine->globalThis().get(String::newString("container_result"));
        REQUIRE(result.isString());
        REQUIRE(result.asString().getValue() == "dep-41");
    }

    // portable deployment: copy the container elsewhere, delete every source
    // file, switch the working directory away from the compile root: the
    // runtime must resolve everything through the tables
    {
        auto deployedDir = dir.dir / "deployed";
        std::filesystem::create_directories(deployedDir);
        std::filesystem::copy_file(dir.dir / "app.jsc", deployedDir / "app.jsc");
        for (auto const& entry : std::filesystem::directory_iterator(dir.dir)) {
            if (entry.path().extension() == ".js") {
                std::filesystem::remove(entry.path());
            }
        }
        auto oldCwd = std::filesystem::current_path();
        std::filesystem::current_path(deployedDir);

        auto        engine2 = makeEngine();
        EngineScope scope2{engine2.get()};
        try {
            REQUIRE_NOTHROW(engine2->loadByteCodeContainer(deployedDir / "app.jsc"));
            auto result = engine2->globalThis().get(String::newString("container_result"));
            REQUIRE(result.isString());
            REQUIRE(result.asString().getValue() == "dep-41");
        } catch (...) {
            std::filesystem::current_path(oldCwd);
            throw;
        }
        std::filesystem::current_path(oldCwd);
    }
}

// ============================================================================
// Loading failure state and multi-container semantics
// ============================================================================

// A container whose dependency deserializes fine but whose entry blob is
// corrupt: the outer container checksum is recomputed so the failure hits the
// inner quickjs blob reader, not the container wrapper.
std::vector<std::uint8_t> makeContainerWithBrokenEntryBlob(ScopedTempDir& dir) {
    dir.writeFile("dep.js", "export const TAG = 'first-load';\n");
    dir.writeFile("main.js", "import { TAG } from './dep.js';\nexport const M = TAG;\n");
    auto bytes  = dir.compile("main.js");
    auto parsed = nonstd::parseBytecodeContainer(bytes);
    // flip a byte inside the entry payload (payload offsets are file offsets
    // into the raw container bytes), then refresh the outer checksum so the
    // container still parses and the failure reaches JS_ReadObject
    auto entry           = parsed.findModule("main.js");
    bytes[entry->start] ^= 0xFF;
    refreshChecksum(bytes);
    return bytes;
}

// A container whose first-read dependency succeeds while a later one fails:
// the read order is dep2, dep1, entry: dep2's value is already taken when
// dep1's blob read fails, exercising the ownership guard mid-loop.
std::vector<std::uint8_t> makeContainerWithBrokenDependencyBlob(ScopedTempDir& dir) {
    dir.writeFile("dep2.js", "export const LEAF = 'leaf';\n");
    dir.writeFile("dep1.js", "import { LEAF } from './dep2.js';\nexport const MID = 'mid-' + LEAF;\n");
    dir.writeFile("main.js", "import { MID } from './dep1.js';\nexport const M = MID;\n");
    auto bytes          = dir.compile("main.js");
    auto parsed         = nonstd::parseBytecodeContainer(bytes);
    auto dep1           = parsed.findModule("dep1.js");
    bytes[dep1->start] ^= 0xFF;
    refreshChecksum(bytes);
    return bytes;
}

TEST_CASE("Engine: failed container load leaves a defined, consistent state", "[bytecode-container]") {
    ScopedTempDir dir("jspp_container_fail_state");
    auto          broken = makeContainerWithBrokenEntryBlob(dir);
    dir.writeBinary("broken.jsc", broken);

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};

    // the failure names the module, the stage, and quickjs's underlying cause
    REQUIRE_THROWS_WITH(
        engine->loadByteCodeContainer(dir.dir / "broken.jsc"),
        Catch::Matchers::ContainsSubstring("main.js") && Catch::Matchers::ContainsSubstring("deserialization")
            && Catch::Matchers::ContainsSubstring("Failed to read container module")
    );

    // no pending exception is left on the engine: plain scripts run normally
    REQUIRE_NOTHROW(engine->evalScript(String::newString("globalThis.after_fail = 42;")));
    REQUIRE(engine->globalThis().get(String::newString("after_fail")).asNumber().getInt32() == 42);

    // the dependency deserialized before the failure stays registered in
    // quickjs (no public unregister API), and the engine keeps its container
    // mapping. The engine therefore counts as occupied: any later container
    // load is rejected, so the surviving definitions can never be shadowed by
    // a different container's copy.
    ScopedTempDir dir2("jspp_container_fail_state2");
    dir2.writeFile("dep.js", "export const TAG = 'second-load';\n");
    dir2.writeFile("main.js", "import { TAG } from './dep.js';\nexport const M = TAG;\n");
    dir2.writeBinary("second.jsc", dir2.compile("main.js"));

    // control group: a fresh engine loads the second container normally
    auto        engine2 = makeEngine();
    EngineScope scope2{engine2.get()};
    {
        auto ns = engine2->loadByteCodeContainerNamespace(dir2.dir / "second.jsc");
        auto m  = ns.get(String::newString("M"));
        REQUIRE(m.isString());
        REQUIRE(m.asString().getValue() == "second-load");
    }
    // on an engine whose failed load already registered dep.js, the second
    // container cannot be loaded at all (single-container rule)
    {
        auto        engine3 = makeEngine();
        EngineScope scope3{engine3.get()};
        REQUIRE_THROWS(engine3->loadByteCodeContainer(dir.dir / "broken.jsc"));
        REQUIRE_THROWS_WITH(
            engine3->loadByteCodeContainer(dir2.dir / "second.jsc"),
            Catch::Matchers::ContainsSubstring("already loaded")
        );
        // the engine still runs plain scripts after both rejections
        REQUIRE_NOTHROW(engine3->evalScript(String::newString("globalThis.failed_twice = 7;")));
        REQUIRE(engine3->globalThis().get(String::newString("failed_twice")).asNumber().getInt32() == 7);
    }
}

TEST_CASE("Engine: mid-deserialization failure keeps the engine usable", "[bytecode-container]") {
    // the read order is dep2, dep1, entry: dep2's value is taken first, then
    // dep1's corrupt blob fails; the already-taken value must be released by
    // the guard before the exception escapes
    {
        ScopedTempDir dir("jspp_container_mid_fail_leaf");
        auto          bytes = makeContainerWithBrokenDependencyBlob(dir);
        dir.writeBinary("broken-mid.jsc", bytes);

        auto        engine = makeEngine();
        EngineScope scope{engine.get()};
        REQUIRE_THROWS_WITH(
            engine->loadByteCodeContainer(dir.dir / "broken-mid.jsc"),
            Catch::Matchers::ContainsSubstring("dep1.js") && Catch::Matchers::ContainsSubstring("deserialization")
        );
        // no pending exception residue: plain scripts keep working
        REQUIRE_NOTHROW(engine->evalScript(String::newString("globalThis.mid_ok = 1;")));
        REQUIRE(engine->globalThis().get(String::newString("mid_ok")).asNumber().getInt32() == 1);
    }
    // first read fails immediately (no value taken yet)
    {
        ScopedTempDir dir("jspp_container_mid_fail_leaf2");
        dir.writeFile("dep2.js", "export const LEAF = 'leaf';\n");
        dir.writeFile("dep1.js", "import { LEAF } from './dep2.js';\nexport const MID = 'mid-' + LEAF;\n");
        dir.writeFile("main.js", "import { MID } from './dep1.js';\nexport const M = MID;\n");
        auto bytes          = dir.compile("main.js");
        auto parsed         = nonstd::parseBytecodeContainer(bytes);
        auto dep2           = parsed.findModule("dep2.js");
        bytes[dep2->start] ^= 0xFF;
        refreshChecksum(bytes);
        dir.writeBinary("broken-leaf.jsc", bytes);

        auto        engine = makeEngine();
        EngineScope scope{engine.get()};
        REQUIRE_THROWS_WITH(
            engine->loadByteCodeContainer(dir.dir / "broken-leaf.jsc"),
            Catch::Matchers::ContainsSubstring("dep2.js")
        );
        REQUIRE_NOTHROW(engine->evalScript(String::newString("globalThis.leaf_fail_ok = 3;")));
        REQUIRE(engine->globalThis().get(String::newString("leaf_fail_ok")).asNumber().getInt32() == 3);
    }
    // several dependencies read fine and the entry blob fails: the values taken
    // before the failure must be released by the guard
    {
        ScopedTempDir dir("jspp_container_mid_fail_entry");
        dir.writeFile("dep2.js", "export const LEAF = 'leaf';\n");
        dir.writeFile("dep1.js", "import { LEAF } from './dep2.js';\nexport const MID = 'mid-' + LEAF;\n");
        dir.writeFile("main.js", "import { MID } from './dep1.js';\nexport const M = MID;\n");
        auto bytes           = dir.compile("main.js");
        auto parsed          = nonstd::parseBytecodeContainer(bytes);
        auto entry           = parsed.findModule("main.js");
        bytes[entry->start] ^= 0xFF;
        refreshChecksum(bytes);
        dir.writeBinary("broken-entry.jsc", bytes);

        auto        engine = makeEngine();
        EngineScope scope{engine.get()};
        REQUIRE_THROWS_WITH(
            engine->loadByteCodeContainer(dir.dir / "broken-entry.jsc"),
            Catch::Matchers::ContainsSubstring("main.js")
        );
        REQUIRE_NOTHROW(engine->evalScript(String::newString("globalThis.entry_fail_ok = 2;")));
        REQUIRE(engine->globalThis().get(String::newString("entry_fail_ok")).asNumber().getInt32() == 2);
    }
}

TEST_CASE("Engine: entry link failure and evaluation exception propagate cleanly", "[bytecode-container]") {
    // link failure: the entry imports an export the dependency does not
    // provide. performEvaluateModule takes over the entry value and releases
    // it on this path; the guard must not release it a second time.
    {
        ScopedTempDir dir("jspp_container_link_fail");
        dir.writeFile("dep.js", "export const A = 1;\n");
        dir.writeFile("main.js", "import { MISSING } from './dep.js';\nexport const M = MISSING;\n");
        dir.writeBinary("app.jsc", dir.compile("main.js"));

        auto        engine = makeEngine();
        EngineScope scope{engine.get()};
        REQUIRE_THROWS_WITH(
            engine->loadByteCodeContainer(dir.dir / "app.jsc"),
            Catch::Matchers::ContainsSubstring("MISSING")
        );
        REQUIRE_NOTHROW(engine->evalScript(String::newString("globalThis.link_fail_ok = 1;")));
        REQUIRE(engine->globalThis().get(String::newString("link_fail_ok")).asNumber().getInt32() == 1);
    }
    // evaluation exception: the entry's top-level code throws at run time.
    // JS_EvalFunction has consumed the entry value; the rejected-promise path
    // in performEvaluateModule releases it before throwing.
    {
        ScopedTempDir dir("jspp_container_eval_throw");
        dir.writeFile("dep.js", "export const A = 1;\n");
        dir.writeFile(
            "main.js",
            "import { A } from './dep.js';\nglobalThis.before_throw = true;\nthrow new Error('entry blew up');\n"
        );
        dir.writeBinary("app.jsc", dir.compile("main.js"));

        auto        engine = makeEngine();
        EngineScope scope{engine.get()};
        REQUIRE_THROWS_WITH(
            engine->loadByteCodeContainer(dir.dir / "app.jsc"),
            Catch::Matchers::ContainsSubstring("entry blew up")
        );
        // the side effect before the throw is real; the engine stays usable
        REQUIRE(engine->globalThis().get(String::newString("before_throw")).asBoolean().getValue() == true);
        REQUIRE_NOTHROW(engine->evalScript(String::newString("globalThis.eval_throw_ok = 2;")));
        REQUIRE(engine->globalThis().get(String::newString("eval_throw_ok")).asNumber().getInt32() == 2);
    }
}

TEST_CASE("Engine: shared dependency evaluates exactly once", "[bytecode-container]") {
    ScopedTempDir dir("jspp_container_diamond");
    dir.writeFile("main.js", R"(
        import { BUMP } from './left.js';
        import { READ } from './right.js';
        globalThis.diamond_result = READ();
    )");
    dir.writeFile(
        "base.js",
        "globalThis.base_evals = (globalThis.base_evals ?? 0) + 1;\nexport function V() { return "
        "globalThis.base_evals; }\n"
    );
    dir.writeFile("left.js", "import { V } from './base.js';\nexport function BUMP() { return V(); }\n");
    dir.writeFile("right.js", "import { V } from './base.js';\nexport function READ() { return V(); }\n");
    dir.writeBinary("app.jsc", dir.compile("main.js"));

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};
    REQUIRE_NOTHROW(engine->loadByteCodeContainer(dir.dir / "app.jsc"));

    // the base module's top-level code ran exactly once despite two importers
    REQUIRE(engine->globalThis().get(String::newString("base_evals")).asNumber().getInt32() == 1);
    REQUIRE(engine->globalThis().get(String::newString("diamond_result")).asNumber().getInt32() == 1);
}

TEST_CASE("Engine: dependency modules resolve dynamic imports and expose no import.meta", "[bytecode-container]") {
    // control group: the same structure loaded from source via evalModule;
    // dynamic import inside a non-entry module must behave identically
    {
        ScopedTempDir srcDir("jspp_container_dep_meta_source");
        srcDir.writeFile("main.js", "export const ENTRY = 1;\n");
        srcDir.writeFile("dep.js", R"(
            import { VALUE } from './num.js';
            import('./num.js').then(m => {
                globalThis.src_dep_dynamic = m.VALUE;
            }, err => {
                globalThis.src_dep_dynamic = 'rejected: ' + err;
            });
        )");
        srcDir.writeFile("num.js", "export const VALUE = 9;\n");

        auto        engine = makeEngine();
        EngineScope scope{engine.get()};
        REQUIRE_NOTHROW(
            engine->evalModule(String::newString("import './dep.js';"), String::newString(srcDir.getFileUrl("main.js")))
        );
        engine->getJobQueue()->shutdown(true);
        engine->getJobQueue()->loopAndWait();
        REQUIRE(engine->globalThis().get(String::newString("src_dep_dynamic")).asNumber().getInt32() == 9);
    }

    ScopedTempDir dir("jspp_container_dep_meta");
    // the entry must import dep.js: the container only bundles modules
    // reachable from the entry
    dir.writeFile("main.js", "import { DEP_VALUE } from './dep.js';\nexport const ENTRY = DEP_VALUE;\n");
    dir.writeFile("dep.js", R"(
        import { VALUE } from './num.js';
        import('./num.js').then(m => {
            globalThis.dep_dynamic = m.VALUE;
        }, err => {
            globalThis.dep_dynamic = 'rejected: ' + err;
        });
        globalThis.dep_meta_url = import.meta.url;
        globalThis.dep_meta_main = import.meta.main;
        export const DEP_VALUE = VALUE;
    )");
    dir.writeFile("num.js", "export const VALUE = 9;\n");
    dir.writeBinary("app.jsc", dir.compile("main.js"));

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};
    REQUIRE_NOTHROW(engine->loadByteCodeContainer(dir.dir / "app.jsc"));
    engine->getJobQueue()->shutdown(true);
    engine->getJobQueue()->loopAndWait();

    // dynamic import inside a non-entry module resolves through the tables
    auto depDynamic = engine->globalThis().get(String::newString("dep_dynamic"));
    INFO("dep_dynamic = " + (depDynamic.isString() ? depDynamic.asString().getValue() : std::string("(not set)")));
    REQUIRE(depDynamic.isNumber());
    REQUIRE(depDynamic.asNumber().getInt32() == 9);

    // v1 semantics: dependency modules have no import.meta.url / .main; only
    // the entry does
    auto url = engine->globalThis().get(String::newString("dep_meta_url"));
    REQUIRE(url.isUndefined());
    auto mainFlag = engine->globalThis().get(String::newString("dep_meta_main"));
    REQUIRE(mainFlag.isUndefined());
}

TEST_CASE("Engine: same-name containers stay isolated across engines", "[bytecode-container]") {
    ScopedTempDir dir("jspp_container_cross_engine");
    dir.writeFile("a/main.js", "import { TAG } from './tag.js';\nglobalThis.tag_result = TAG;\n");
    dir.writeFile("a/tag.js", "export const TAG = 'engine-a';\n");
    dir.writeFile("b/main.js", "import { TAG } from './tag.js';\nglobalThis.tag_result = TAG;\n");
    dir.writeFile("b/tag.js", "export const TAG = 'engine-b';\n");

    auto a = scriptc::compileModuleContainer(dir.dir / "a", "main.js", {}, false);
    auto b = scriptc::compileModuleContainer(dir.dir / "b", "main.js", {}, false);
    dir.writeBinary("a.jsc", a);
    dir.writeBinary("b.jsc", b);

    // the same module names loaded into two engines resolve independently:
    // container state lives on the engine instance
    auto engineA = makeEngine();
    {
        EngineScope scope{engineA.get()};
        REQUIRE_NOTHROW(engineA->loadByteCodeContainer(dir.dir / "a.jsc"));
        REQUIRE(engineA->globalThis().get(String::newString("tag_result")).asString().getValue() == "engine-a");
    }
    auto engineB = makeEngine();
    {
        EngineScope scope{engineB.get()};
        REQUIRE_NOTHROW(engineB->loadByteCodeContainer(dir.dir / "b.jsc"));
        REQUIRE(engineB->globalThis().get(String::newString("tag_result")).asString().getValue() == "engine-b");
    }
}

TEST_CASE("Engine: corrupt inner blob reports module and stage, engine stays usable", "[bytecode-container]") {
    ScopedTempDir dir("jspp_container_inner_blob");
    writeStandardGraph(dir);
    dir.writeBinary("good.jsc", dir.compile("main.js"));

    // corrupt a byte inside the num.js payload only, keeping the outer
    // container checksum valid (payload offsets index the raw container bytes)
    auto bytes          = dir.readBinary("good.jsc");
    auto parsed         = nonstd::parseBytecodeContainer(bytes);
    auto blob           = parsed.findModule("num.js");
    bytes[blob->start] ^= 0xFF;
    refreshChecksum(bytes);
    dir.writeBinary("bad-inner.jsc", bytes);

    auto        engine = makeEngine();
    EngineScope scope{engine.get()};
    REQUIRE_THROWS_WITH(
        engine->loadByteCodeContainer(dir.dir / "bad-inner.jsc"),
        Catch::Matchers::ContainsSubstring("num.js") && Catch::Matchers::ContainsSubstring("deserialization")
    );
    // the engine's exception state was consumed: plain scripts keep working
    REQUIRE_NOTHROW(engine->evalScript(String::newString("globalThis.recovered = true;")));
    REQUIRE(engine->globalThis().get(String::newString("recovered")).asBoolean().getValue() == true);
}

#endif // JSPP_BACKEND_QUICKJS
