#include "BytecodeContainer.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace jspp::qjs_backend::nonstd {
namespace {

constexpr std::uint64_t kFnv1a64OffsetBasis = 14695981039346656037ULL;
constexpr std::uint64_t kFnv1a64Prime       = 1099511628211ULL;

constexpr std::uint64_t fnv1a64(std::span<std::uint8_t const> data) {
    std::uint64_t hash = kFnv1a64OffsetBasis;
    for (auto byte : data) {
        hash ^= byte;
        hash *= kFnv1a64Prime;
    }
    return hash;
}

/** @brief Bounds-checked little endian reader over the raw container bytes. */
struct ByteReader {
    std::span<std::uint8_t const> data;
    std::size_t                   pos = 0;

    [[noreturn]] void truncated() const {
        throw std::runtime_error(
            "bytecode container: truncated at offset " + std::to_string(pos) + " (size " + std::to_string(data.size())
            + ")"
        );
    }

    void ensure(std::size_t count) const {
        if (count > data.size() - pos) [[unlikely]] {
            truncated();
        }
    }

    std::uint32_t u32() {
        ensure(4);
        std::uint32_t value;
        if constexpr (std::endian::native == std::endian::little) {
            std::memcpy(&value, data.data() + pos, sizeof(value));
        } else {
            value = std::uint32_t(data[pos]) | std::uint32_t(data[pos + 1]) << 8 | std::uint32_t(data[pos + 2]) << 16
                  | std::uint32_t(data[pos + 3]) << 24;
        }
        pos += 4;
        return value;
    }

    std::uint64_t u64() {
        ensure(8);
        std::uint64_t value;
        if constexpr (std::endian::native == std::endian::little) {
            std::memcpy(&value, data.data() + pos, sizeof(value));
        } else {
            for (int i = 7; i >= 0; i--) {
                value = (value << 8) | std::uint64_t(data[pos + i]);
            }
        }
        pos += 8;
        return value;
    }

    std::string string(std::size_t count) {
        ensure(count);
        auto value  = std::string{reinterpret_cast<char const*>(data.data() + pos), count};
        pos        += count;
        return value;
    }
};

constexpr std::size_t kHeaderSize          = 4 + 4 + 8 + 4;   // magic, version, checksum, module_count
constexpr std::size_t kChecksumCoverOffset = kHeaderSize - 4; // checksum covers [module_count .. EOF]

// stores one little endian u32 at an arbitrary offset inside the buffer
// (used to patch the payload offset fields once their base is known)
void storeU32AsLittleEndian(std::vector<std::uint8_t>& buffer, std::size_t offset, std::uint32_t value) {
    if constexpr (std::endian::native == std::endian::little) {
        std::memcpy(buffer.data() + offset, &value, sizeof(value));
    } else {
        buffer[offset]     = std::uint8_t(value);
        buffer[offset + 1] = std::uint8_t(value >> 8);
        buffer[offset + 2] = std::uint8_t(value >> 16);
        buffer[offset + 3] = std::uint8_t(value >> 24);
    }
}

// True when the specifier addresses a module inside the compilation root.
// Bare, rooted and other non-relative specifiers belong to native modules and
// never enter the container tables.
constexpr bool isRelativeSpecifier(std::string_view specifier) {
    return specifier.starts_with("./") || specifier.starts_with("../");
}

} // namespace

std::string normalizeModuleName(std::string_view name) {
    if (name.empty()) {
        throw std::runtime_error("module name must not be empty");
    }
    if (name.back() == '/') {
        throw std::runtime_error("module name must reference a file: '" + std::string{name} + "'");
    }
    if (name.find('\0') != std::string_view::npos) {
        throw std::runtime_error("module name must not contain NUL bytes: '" + std::string{name} + "'");
    }
    if (name.find('\\') != std::string_view::npos) {
        throw std::runtime_error("module name must use '/' separators: '" + std::string{name} + "'");
    }
    if (name.starts_with('/')) {
        throw std::runtime_error("module name must be root-relative, not absolute: '" + std::string{name} + "'");
    }
    // Windows drive prefix ("C:" or "C:/..."): a single letter followed by ':'
    if (name.size() >= 2 && name[1] == ':' && (name[0] >= 'A' && name[0] <= 'Z' || name[0] >= 'a' && name[0] <= 'z')) {
        throw std::runtime_error("module name must not carry a drive prefix: '" + std::string{name} + "'");
    }

    std::vector<std::string_view> parts;
    std::size_t                   start = 0;
    while (start <= name.size()) {
        auto slash = name.find('/', start);
        auto part  = name.substr(start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        bool last  = slash == std::string_view::npos;
        if (part == "..") {
            throw std::runtime_error("module name must not contain '..' segments: '" + std::string{name} + "'");
        }
        if (!part.empty() && part != ".") {
            parts.push_back(part);
        }
        if (last) {
            break;
        }
        start = slash + 1;
    }
    if (parts.empty()) {
        throw std::runtime_error("module name must reference a file: '" + std::string{name} + "'");
    }

    std::string canonical;
    for (auto part : parts) {
        if (!canonical.empty()) {
            canonical += '/';
        }
        canonical += part;
    }
    return canonical;
}

BytecodeModuleEntry const* BytecodeContainer::findModule(std::string_view name) const {
    for (auto const& module : modules) {
        if (module.name == name) {
            return &module;
        }
    }
    return nullptr;
}

BytecodeImportEdge const*
BytecodeContainer::findImport(std::string_view importerName, std::string_view specifier) const {
    for (auto const& edge : imports) {
        if (edge.specifier == specifier && modules[edge.importerIndex].name == importerName) {
            return &edge;
        }
    }
    return nullptr;
}

std::span<std::uint8_t const> BytecodeContainer::payloadOf(BytecodeModuleEntry const& entry) const {
    return {raw.data() + entry.start, static_cast<std::size_t>(entry.end - entry.start)};
}

BytecodeContainer parseBytecodeContainer(std::span<std::uint8_t const> bytes) {
    ByteReader reader{bytes};
    reader.ensure(kHeaderSize);

    if (!hasContainerMagic(bytes)) [[unlikely]] {
        throw std::runtime_error("bytecode container: bad magic (expected 'JSC1')");
    }
    reader.pos         = 4; // past the magic, already validated
    auto formatVersion = reader.u32();
    if (formatVersion != kContainerFormatVersion) [[unlikely]] {
        throw std::runtime_error("bytecode container: unsupported format version " + std::to_string(formatVersion));
    }
    auto storedChecksum = reader.u64();
    auto actualChecksum = fnv1a64(bytes.subspan(kChecksumCoverOffset));
    if (storedChecksum != actualChecksum) [[unlikely]] {
        throw std::runtime_error("bytecode container: checksum mismatch");
    }

    BytecodeContainer container;
    container.raw.assign(bytes.begin(), bytes.end());

    auto moduleCount = reader.u32();
    if (moduleCount == 0) [[unlikely]] {
        throw std::runtime_error("bytecode container: empty module table");
    }
    // the declared counts are attacker-controlled: bound them by the remaining
    // input before any allocation, using each record's minimum encoded size
    constexpr std::size_t kMinModuleRecord = 4 + 1 + 4 + 4; // name_len + 1 char + start + end
    if (moduleCount > (bytes.size() - reader.pos) / kMinModuleRecord) [[unlikely]] {
        reader.truncated();
    }
    container.modules.reserve(moduleCount);
    for (std::uint32_t i = 0; i < moduleCount; i++) {
        BytecodeModuleEntry module;
        auto                nameLen = reader.u32();
        module.name                 = reader.string(nameLen);
        module.start                = reader.u32();
        module.end                  = reader.u32();
        // names must already be canonical: baked names and runtime lookups are
        // compared verbatim, so "./a.js" and "a.js" would be two modules
        auto canonical = normalizeModuleName(module.name);
        if (canonical != module.name) [[unlikely]] {
            throw std::runtime_error(
                "bytecode container: module name '" + module.name + "' is not canonical (expected '" + canonical + "')"
            );
        }
        if (container.findModule(module.name) != nullptr) [[unlikely]] {
            throw std::runtime_error("bytecode container: duplicate module name '" + module.name + "'");
        }
        container.modules.push_back(std::move(module));
    }

    auto                  importCount      = reader.u32();
    constexpr std::size_t kMinImportRecord = 4 + 4 + 1 + 4; // importer + len + 1 char + target
    if (importCount > (bytes.size() - reader.pos) / kMinImportRecord) [[unlikely]] {
        reader.truncated();
    }
    container.imports.reserve(importCount);
    for (std::uint32_t i = 0; i < importCount; i++) {
        BytecodeImportEdge edge;
        edge.importerIndex = reader.u32();
        auto specifierLen  = reader.u32();
        edge.specifier     = reader.string(specifierLen);
        edge.targetIndex   = reader.u32();
        if (edge.importerIndex >= moduleCount || edge.targetIndex >= moduleCount) [[unlikely]] {
            throw std::runtime_error(
                "bytecode container: import table entry " + std::to_string(i)
                + " references a module index out of range"
            );
        }
        if (!isRelativeSpecifier(edge.specifier)) [[unlikely]] {
            throw std::runtime_error(
                "bytecode container: import specifier '" + edge.specifier
                + "' is not relative (bare imports belong to native modules, not the tables)"
            );
        }
        for (auto const& existing : container.imports) {
            if (existing.importerIndex == edge.importerIndex && existing.specifier == edge.specifier) [[unlikely]] {
                throw std::runtime_error(
                    "bytecode container: duplicate import key '" + edge.specifier + "' of module '"
                    + container.modules[edge.importerIndex].name + "'"
                );
            }
        }
        container.imports.push_back(std::move(edge));
    }

    // Payload layout: the sections must exactly tile everything after the
    // tables, with no gaps and no overlap (validated by physical offset, not
    // by module table order). This rules out offsets that point into the
    // header, the tables, or past the file, and trailing bytes that no module
    // owns.
    std::size_t payloadBegin = reader.pos;
    {
        std::vector<std::pair<std::uint32_t, std::uint32_t>> spans;
        spans.reserve(container.modules.size());
        for (auto const& module : container.modules) {
            if (module.start < payloadBegin || module.end > bytes.size() || module.start >= module.end) [[unlikely]] {
                throw std::runtime_error(
                    "bytecode container: payload of module '" + module.name + "' lies outside the payload region"
                );
            }
            spans.emplace_back(module.start, module.end);
        }
        std::sort(spans.begin(), spans.end());
        std::uint32_t cursor = static_cast<std::uint32_t>(payloadBegin);
        for (auto [start, end] : spans) {
            if (start < cursor) [[unlikely]] {
                throw std::runtime_error("bytecode container: overlapping payload sections");
            }
            if (start > cursor) [[unlikely]] {
                throw std::runtime_error("bytecode container: gap between payload sections");
            }
            cursor = end;
        }
        if (cursor != bytes.size()) [[unlikely]] {
            throw std::runtime_error("bytecode container: trailing bytes after the last payload section");
        }
    }

    // v1 requires the module table to hold exactly the entry-reachable graph:
    // unreachable entries would stay registered in the module table while
    // never being deserialized
    {
        std::vector<std::vector<std::uint32_t>> adjacency(moduleCount);
        for (auto const& edge : container.imports) {
            adjacency[edge.importerIndex].push_back(edge.targetIndex);
        }
        std::vector<bool>          reached(moduleCount, false);
        std::vector<std::uint32_t> pending{0};
        reached[0] = true;
        while (!pending.empty()) {
            auto node = pending.back();
            pending.pop_back();
            for (auto target : adjacency[node]) {
                if (!reached[target]) {
                    reached[target] = true;
                    pending.push_back(target);
                }
            }
        }
        for (std::uint32_t i = 0; i < moduleCount; i++) {
            if (!reached[i]) [[unlikely]] {
                throw std::runtime_error(
                    "bytecode container: module '" + container.modules[i].name + "' is not reachable from the entry"
                );
            }
        }
    }
    return container;
}

std::vector<std::uint8_t> writeBytecodeContainer(
    std::vector<ContainerModuleInput> const& modules,
    std::vector<ContainerImportInput> const& imports
) {
    if (modules.empty()) {
        throw std::runtime_error("bytecode container: refusing to write an empty module table");
    }
    // encoding-level input validation: the writer does not re-derive the
    // import graph, so reachability of the result is checked by the parser at
    // load time, not here
    for (auto const& module : modules) {
        auto canonical = normalizeModuleName(module.name);
        if (canonical != module.name) {
            throw std::runtime_error(
                "bytecode container: module name '" + module.name + "' is not canonical (expected '" + canonical + "')"
            );
        }
        if (module.blob.empty()) {
            throw std::runtime_error("bytecode container: module '" + module.name + "' has an empty payload");
        }
        if (module.name.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("bytecode container: module name exceeds the u32 length limit");
        }
    }
    for (std::size_t i = 0; i < modules.size(); i++) {
        for (std::size_t j = 0; j < i; j++) {
            if (modules[i].name == modules[j].name) {
                throw std::runtime_error("bytecode container: duplicate module name '" + modules[i].name + "'");
            }
        }
    }
    for (auto const& edge : imports) {
        if (edge.importerIndex >= modules.size() || edge.targetIndex >= modules.size()) {
            throw std::runtime_error("bytecode container: import edge index out of range");
        }
        if (!isRelativeSpecifier(edge.specifier) || edge.specifier.empty()) {
            throw std::runtime_error(
                "bytecode container: import specifier '" + edge.specifier
                + "' is not relative (bare imports belong to native modules, not the tables)"
            );
        }
        if (edge.specifier.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("bytecode container: import specifier exceeds the u32 length limit");
        }
        for (auto const& existing : imports) {
            if (&existing == &edge) {
                break;
            }
            if (existing.importerIndex == edge.importerIndex && existing.specifier == edge.specifier) {
                throw std::runtime_error(
                    "bytecode container: duplicate import key '" + edge.specifier + "' of module '"
                    + modules[edge.importerIndex].name + "'"
                );
            }
        }
    }
    if (modules.size() > std::numeric_limits<std::uint32_t>::max()
        || imports.size() > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("bytecode container: table count exceeds the u32 limit");
    }

    std::size_t totalSize = kHeaderSize;
    for (auto const& module : modules) {
        totalSize += 4 + module.name.size() + 4 + 4 + module.blob.size();
    }
    totalSize += 4; // import_count
    for (auto const& edge : imports) {
        totalSize += 4 + 4 + edge.specifier.size() + 4;
    }
    if (totalSize > std::numeric_limits<std::uint32_t>::max()) {
        throw std::runtime_error("bytecode container: file exceeds the 4 GiB format limit");
    }

    std::vector<std::uint8_t> out;
    out.reserve(totalSize);

    auto putU32 = [&out](std::uint32_t value) {
        out.push_back(std::uint8_t(value));
        out.push_back(std::uint8_t(value >> 8));
        out.push_back(std::uint8_t(value >> 16));
        out.push_back(std::uint8_t(value >> 24));
    };
    auto putBytes  = [&out](std::span<std::uint8_t const> value) { out.insert(out.end(), value.begin(), value.end()); };
    auto putString = [&out](std::string_view value) {
        out.insert(
            out.end(),
            reinterpret_cast<std::uint8_t const*>(value.data()),
            reinterpret_cast<std::uint8_t const*>(value.data()) + value.size()
        );
    };

    putU32(kContainerMagic);
    putU32(kContainerFormatVersion);
    out.insert(out.end(), 8, 0); // checksum placeholder, filled once the payload is known
    putU32(static_cast<std::uint32_t>(modules.size()));

    // payload offsets are absolute file offsets: remember where each module
    // table entry keeps its start/end fields so they can be patched after the
    // payload base is known
    std::vector<std::size_t> startFields;
    std::vector<std::size_t> endFields;
    startFields.reserve(modules.size());
    endFields.reserve(modules.size());
    for (auto const& module : modules) {
        putU32(static_cast<std::uint32_t>(module.name.size()));
        putString(module.name);
        startFields.push_back(out.size());
        putU32(0); // start placeholder
        endFields.push_back(out.size());
        putU32(0); // end placeholder
    }

    putU32(static_cast<std::uint32_t>(imports.size()));
    for (auto const& edge : imports) {
        putU32(edge.importerIndex);
        putU32(static_cast<std::uint32_t>(edge.specifier.size()));
        putString(edge.specifier);
        putU32(edge.targetIndex);
    }

    std::uint32_t offset = static_cast<std::uint32_t>(out.size());
    for (std::size_t i = 0; i < modules.size(); i++) {
        storeU32AsLittleEndian(out, startFields[i], offset);
        offset += static_cast<std::uint32_t>(modules[i].blob.size());
        storeU32AsLittleEndian(out, endFields[i], offset);
        putBytes(modules[i].blob);
    }

    auto checksum = fnv1a64({out.data() + kChecksumCoverOffset, out.size() - kChecksumCoverOffset});
    for (int i = 0; i < 8; i++) {
        out[8 + i] = std::uint8_t(checksum >> (8 * i));
    }
    return out;
}

std::vector<std::uint32_t>
containerReadOrder(std::span<std::string const> moduleNames, std::span<BytecodeImportEdge const> imports) {
    auto moduleCount = moduleNames.size();
    if (moduleCount == 0) {
        throw std::runtime_error("bytecode container: no modules to order");
    }

    std::vector<std::vector<std::uint32_t>> adjacency(moduleCount);
    for (auto const& edge : imports) {
        if (edge.importerIndex >= moduleCount || edge.targetIndex >= moduleCount) {
            throw std::runtime_error(
                "bytecode container: import edge index out of range (importer " + std::to_string(edge.importerIndex)
                + ", target " + std::to_string(edge.targetIndex) + ")"
            );
        }
        adjacency[edge.importerIndex].push_back(edge.targetIndex);
    }

    enum class Mark : std::uint8_t { Unvisited, Visiting, Done };
    std::vector<Mark>          marks(moduleCount, Mark::Unvisited);
    std::vector<std::uint32_t> order;
    order.reserve(moduleCount);

    // iterative depth-first search so a deep module graph cannot overflow the
    // native stack; the search stack doubles as cycle path diagnostics
    struct Frame {
        std::uint32_t node;
        std::size_t   nextEdge;
    };
    std::vector<Frame> stack;

    // the gray set is exactly the node stack, so a Visiting target is a cycle:
    // report the path from its first occurrence back to itself
    auto throwCycle = [&](std::uint32_t repeated) {
        std::string path    = moduleNames[repeated];
        bool        inCycle = false;
        for (auto const& frame : stack) {
            if (!inCycle && frame.node == repeated) {
                inCycle = true;
                continue;
            }
            if (inCycle) {
                path += " -> " + moduleNames[frame.node];
            }
        }
        path += " -> " + moduleNames[repeated];
        throw std::runtime_error("circular import detected: " + path);
    };

    stack.push_back({0, 0});
    marks[0] = Mark::Visiting;
    while (!stack.empty()) {
        auto& frame = stack.back();
        if (frame.nextEdge < adjacency[frame.node].size()) {
            auto target = adjacency[frame.node][frame.nextEdge++];
            if (marks[target] == Mark::Visiting) {
                throwCycle(target);
            }
            if (marks[target] == Mark::Unvisited) {
                marks[target] = Mark::Visiting;
                stack.push_back({target, 0});
            }
        } else {
            marks[frame.node] = Mark::Done;
            order.push_back(frame.node);
            stack.pop_back();
        }
    }
    return order;
}

} // namespace jspp::qjs_backend::nonstd
