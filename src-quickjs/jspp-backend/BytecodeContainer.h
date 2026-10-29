#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace jspp::qjs_backend::nonstd {

/**
 * @brief Container format written by the scriptc tool ('JSC1').
 *
 * A container bundles the bytecode of one entry module and all of its compiled
 * dependencies into a single deployable file. All integers are little endian;
 * offset fields are u32, so the format limit is 4 GiB.
 *
 * Layout:
 *   Header:
 *     u32 magic       = kContainerMagic
 *     u32 fmt_version = kContainerFormatVersion
 *     u64 checksum    = fnv1a-64 over [module_count .. end of file]
 *     u32 module_count
 *   Module Table (module_count entries, index 0 is the entry module):
 *     u32 name_len, bytes module_name, u32 start, u32 end (exclusive)
 *   Import Table (static import edges captured at compile time):
 *     u32 import_count
 *     per edge: u32 importer_index, u32 specifier_len, bytes specifier,
 *               u32 target_index
 *   Payload: one quickjs bytecode blob per module
 *
 * Rules shared by the writer and the parser:
 *   - Payload sections exactly tile the region between the tables and the end
 *     of the file: no gaps, no overlap, no section inside the tables, no
 *     trailing bytes. The writer emits them in module table order; the parser
 *     validates the tiling by physical offset, not by table order.
 *   - Module names are canonical root-relative logical paths ("main.js",
 *     "lib/dep.js"): no leading "./", absolute paths, drive letters, "." or
 *     ".." segments, or backslashes. Names are unique. Runtime resolution
 *     compares them verbatim.
 *   - Import specifiers are stored verbatim as written in the importing
 *     module and are always relative ("./x.js", "../y.js"): bare or rooted
 *     specifiers belong to native modules and never enter the tables. The
 *     (importer, specifier) key is unique.
 *   - The checksum guards against accidental corruption, not tampering.
 */
inline constexpr std::uint32_t kContainerMagic         = 0x3143534A; // 'JSC1' little endian
inline constexpr std::uint32_t kContainerFormatVersion = 1;

struct BytecodeImportEdge {
    std::uint32_t importerIndex = 0; // index into BytecodeContainer::modules
    std::uint32_t targetIndex   = 0; // index into BytecodeContainer::modules
    std::string   specifier;         // raw specifier as written in the importer
};

struct BytecodeModuleEntry {
    std::string   name;      // canonical root-relative module name
    std::uint32_t start = 0; // payload offset into BytecodeContainer::raw
    std::uint32_t end   = 0; // payload end (exclusive)
};

struct BytecodeContainer {
    std::vector<BytecodeModuleEntry> modules;     // index 0: entry module
    std::vector<BytecodeImportEdge>  imports;     // static import edges
    std::vector<std::uint8_t>        raw;         // whole file; payloads point into it
    std::string                      entrySource; // path recorded by the loader (diagnostics)

    [[nodiscard]] BytecodeModuleEntry const* findModule(std::string_view name) const;

    /**
     * @brief Find the import edge recorded for one raw specifier of one importer.
     * @return nullptr when the container has no such edge.
     */
    [[nodiscard]] BytecodeImportEdge const* findImport(std::string_view importerName, std::string_view specifier) const;

    [[nodiscard]] std::span<std::uint8_t const> payloadOf(BytecodeModuleEntry const& entry) const;
};

/** @brief True when the buffer starts with the container magic bytes. */
[[nodiscard]] inline bool hasContainerMagic(std::span<std::uint8_t const> bytes) {
    return bytes.size() >= 4 && bytes[0] == 'J' && bytes[1] == 'S' && bytes[2] == 'C' && bytes[3] == '1';
}

/**
 * @brief Normalize a module name to its canonical root-relative form.
 *
 * Accepts "./a.js", "a.js", "sub/a.js" and returns "a.js", "a.js", "sub/a.js".
 * Rejects (throws std::runtime_error): empty names, absolute paths, Windows
 * drive prefixes, backslashes, NUL bytes, trailing slashes and ".." segments
 * (imports leaving the compilation root must be declared native instead).
 */
[[nodiscard]] std::string normalizeModuleName(std::string_view name);

/**
 * @brief Parse and validate a container buffer.
 *
 * Beyond the header and checksum, the tables are validated as a whole: module
 * names must be canonical root-relative names and unique, payload offsets must
 * exactly tile the region between the tables and the end of the file (no gaps,
 * no overlap, nothing before the tables or past the file), import edges must
 * use relative specifiers with unique keys, every index must be in range, and
 * every module must be reachable from the entry (index 0).
 *
 * @throw std::runtime_error with a description of the first structural error.
 */
[[nodiscard]] BytecodeContainer parseBytecodeContainer(std::span<std::uint8_t const> bytes);

/** @brief Module input for writeBytecodeContainer; index 0 is the entry. */
struct ContainerModuleInput {
    std::string               name; // canonical root-relative module name
    std::vector<std::uint8_t> blob; // quickjs bytecode (JS_WriteObject output)
};

/** @brief Import edge input for writeBytecodeContainer. */
struct ContainerImportInput {
    std::uint32_t importerIndex = 0;
    std::uint32_t targetIndex   = 0;
    std::string   specifier; // raw relative specifier as written in the importer
};

/**
 * @brief Serialize modules and import edges into container bytes.
 *
 * Internal serializer of the scriptc compiler, which already feeds only the
 * entry-reachable module graph and rejects cycles. The writer enforces the
 * basic encoding contract (canonical unique module names, non-empty payloads,
 * relative specifiers with unique keys, in-range indices, u32 size limits) but
 * does not re-derive the import graph; reachability of the produced container
 * is validated by parseBytecodeContainer at load time.
 *
 * @throw std::runtime_error on empty module table, non-canonical or duplicate
 *        module names, empty payloads, relative-specifier violations, out-of-
 *        range indices and total sizes beyond the u32 offset limit.
 */
[[nodiscard]] std::vector<std::uint8_t> writeBytecodeContainer(
    std::vector<ContainerModuleInput> const& modules,
    std::vector<ContainerImportInput> const& imports
);

/**
 * @brief Module indices in dependency-first read order: import targets before
 *        importers, the entry module (index 0) last.
 *
 * Deserialization must follow this order because reading a module blob
 * resolves its dependencies immediately. Modules unreachable from the entry
 * are omitted.
 *
 * @param moduleNames index-aligned module names, used in diagnostics.
 * @throw std::runtime_error when an edge index is out of range or the import
 *        edges contain a cycle; the cycle message contains the cycle path
 *        (cycles are rejected by scriptc at compile time, so this is a
 *        defensive check for malformed input).
 */
[[nodiscard]] std::vector<std::uint32_t>
containerReadOrder(std::span<std::string const> moduleNames, std::span<BytecodeImportEdge const> imports);

} // namespace jspp::qjs_backend::nonstd
