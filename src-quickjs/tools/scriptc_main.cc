// scriptc: compiles an ES module graph into a bytecode container file.
//
// Based on quickjs-ng qjsc.c (MIT license, (c) Fabrice Bellard, Ben
// Noordhuis, Saul Ibarra Corretge). The compilation core lives in
// scriptc_core.cc so that the jspp test suite can compile containers in
// process; this executable only handles the command line.
//
// The output bundles one QuickJS bytecode blob per module plus a module table
// and an import table (see jspp-backend/BytecodeContainer.h). Bytecode embeds
// a checksum and a format version, so a container is only readable by the
// QuickJS build that produced it: scriptc links the same QuickJS library as
// the jspp runtime.

#include "scriptc_core.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "quickjs.h"

namespace {

void printUsage(std::FILE* stream) {
    std::fprintf(
        stream,
        "Usage: scriptc [OPTIONS] -o FILE <input.js>\n"
        "       scriptc --help\n"
        "       scriptc --version\n"
    );
}

void printHelp() {
    printUsage(stdout);
    std::printf(
        "\n"
        "Compile an ES module graph into a QuickJS bytecode container.\n"
        "\n"
        "Options:\n"
        "  -o FILE   Output container file (required).\n"
        "  -m        Force module compilation of the entry file.\n"
        "  -n NAME   Override the entry module's logical name.\n"
        "  -M NAME   Declare a native module; may be specified multiple times.\n"
        "  --help    Show this help and exit.\n"
        "  --version Show the QuickJS version used to build scriptc.\n"
        "\n"
        "Notes:\n"
        "  The entry file's directory is the compilation root. Relative imports\n"
        "  must resolve inside that directory tree. Imports that leave the tree,\n"
        "  bare specifiers, and rooted specifiers must be declared with -M and\n"
        "  are resolved by the host at runtime.\n"
        "\n"
        "  By default, the entry module's name is its root-relative file name.\n"
        "  -n overrides that name and also becomes the base for resolving the\n"
        "  entry module's relative imports. The override must be a canonical,\n"
        "  non-empty root-relative module name.\n"
        "\n"
        "  The container bundles the entry module and its import closure.\n"
        "  Circular imports are rejected. The runtime must use a compatible\n"
        "  QuickJS build.\n"
        "\n"
    );
}

bool writeOutput(std::string const& outFilename, std::vector<std::uint8_t> const& bytes) {
    std::FILE* file = std::fopen(outFilename.c_str(), "wb");
    if (!file) {
        std::perror(outFilename.c_str());
        return false;
    }

    bool ok = std::fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
    if (std::fclose(file) != 0) {
        ok = false;
    }

    if (!ok) {
        std::fprintf(stderr, "scriptc: could not write '%s'\n", outFilename.c_str());
    }
    return ok;
}

bool requireArgument(int& optind, int argc, char** argv, char const* option, char const*& value) {
    if (optind + 1 >= argc) {
        std::fprintf(stderr, "scriptc: missing argument for %s\n", option);
        return false;
    }

    value = argv[++optind];
    return true;
}

} // namespace

int main(int argc, char** argv) {
    const char* outFilename = nullptr;
    const char* inFilename  = nullptr;
    const char* entryName   = nullptr;

    bool                     forceModule = false;
    std::vector<std::string> nativeModules;

    if (argc == 1) {
        printHelp();
        return 0;
    }

    for (int optind = 1; optind < argc; ++optind) {
        const char* arg = argv[optind];

        if (std::strcmp(arg, "--help") == 0) {
            printHelp();
            return 0;
        }

        if (std::strcmp(arg, "--version") == 0) {
            std::printf("scriptc (QuickJS %d.%d.%d)\n", QJS_VERSION_MAJOR, QJS_VERSION_MINOR, QJS_VERSION_PATCH);
            return 0;
        }

        if (std::strcmp(arg, "-o") == 0) {
            const char* value = nullptr;
            if (!requireArgument(optind, argc, argv, "-o", value)) {
                printUsage(stderr);
                return 1;
            }
            outFilename = value;
        } else if (std::strcmp(arg, "-M") == 0) {
            const char* value = nullptr;
            if (!requireArgument(optind, argc, argv, "-M", value)) {
                printUsage(stderr);
                return 1;
            }
            nativeModules.emplace_back(value);
        } else if (std::strcmp(arg, "-n") == 0) {
            const char* value = nullptr;
            if (!requireArgument(optind, argc, argv, "-n", value)) {
                printUsage(stderr);
                return 1;
            }
            entryName = value;
        } else if (std::strcmp(arg, "-m") == 0) {
            // Force module mode; JS_DetectModule may misclassify files whose
            // first import is relative.
            forceModule = true;
        } else if (arg[0] == '-') {
            std::fprintf(stderr, "scriptc: unknown option '%s'\n", arg);
            printUsage(stderr);
            return 1;
        } else {
            if (inFilename) {
                std::fprintf(stderr, "scriptc: expected a single input file\n");
                printUsage(stderr);
                return 1;
            }
            inFilename = arg;
        }
    }

    if (!outFilename || !inFilename) {
        if (!outFilename) {
            std::fprintf(stderr, "scriptc: missing required option -o FILE\n");
        }
        if (!inFilename) {
            std::fprintf(stderr, "scriptc: missing input file\n");
        }
        printUsage(stderr);
        return 1;
    }

    if (entryName && *entryName == '\0') {
        std::fprintf(stderr, "scriptc: -n requires a non-empty module name\n");
        return 1;
    }

    try {
        // The entry file's directory is the compilation root.
        auto entryPath = std::filesystem::weakly_canonical(inFilename);
        auto rootDir   = entryPath.parent_path();
        auto fileName  = entryPath.filename().string();

        // -n overrides the baked module name, not the physical source path.
        auto bytes = jspp::scriptc::compileModuleContainer(
            rootDir,
            fileName,
            nativeModules,
            forceModule,
            entryName ? std::optional<std::string>{entryName} : std::nullopt
        );

        // Compilation finishes before opening the output file. A compilation
        // failure therefore leaves an existing output file untouched.
        // A disk error during direct overwrite may still truncate the output.
        if (!writeOutput(outFilename, bytes)) {
            return 1;
        }

        return 0;
    } catch (std::exception const& e) {
        std::fprintf(stderr, "scriptc: %s\n", e.what());
        return 1;
    }
}
