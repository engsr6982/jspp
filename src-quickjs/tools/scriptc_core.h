#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace jspp::scriptc {

/**
 * @brief Compile an ES module graph into a scriptc container ('JSC1').
 *
 * The entry module and every module reachable through its relative imports are
 * compiled to quickjs bytecode and bundled into a single container file, along
 * with the import table that records each static import edge. See
 * jspp-backend/BytecodeContainer.h for the format.
 *
 * @param rootDir        compilation root; module names are canonical
 *                       root-relative paths and relative imports resolving
 *                       outside the root are rejected
 * @param entryName      root-relative path of the entry module ("main.js",
 *                       "src/main.js"); must normalize to a canonical
 *                       root-relative name (absolute paths, drive prefixes and
 *                       ".." escapes are rejected), and is baked into the
 *                       entry blob
 * @param nativeModules  bare specifiers declared as native (-M): imported by
 *                       name and resolved by the host at runtime; not contained
 * @param forceModule    compile the entry as a module even when it is not
 *                       detected as one
 * @param bakedEntryName optional override of the entry's baked module name
 *                       (CLI -n). The override defines the entry's logical
 *                       module identity: the baked name doubles as the base
 *                       for resolving the entry's relative imports, so
 *                       "-n alias/main.js" compiles imports against "alias/"
 *                       and requires those files to exist under the root.
 *                       Must be canonical root-relative when provided; an
 *                       empty override is rejected.
 * @return the container file bytes
 * @throw std::runtime_error with the compiler diagnostic (entry not found,
 *        invalid entry name, missing dependency, syntax error anywhere in the
 *        graph, undeclared bare or rooted import specifier, import escaping
 *        the compilation root, circular imports)
 */
std::vector<std::uint8_t> compileModuleContainer(
    std::filesystem::path const&      rootDir,
    std::string const&                entryName,
    std::vector<std::string> const&   nativeModules,
    bool                              forceModule,
    std::optional<std::string> const& bakedEntryName = std::nullopt
);

} // namespace jspp::scriptc


