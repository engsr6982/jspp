// scriptc: compiles an ES module graph into a bytecode container file.
//
// Based on quickjs-ng qjsc.c (MIT license, (c) Fabrice Bellard, Ben
// Noordhuis, Saul Ibarra Corretge). The compilation core lives in
// scriptc_core.cc so that the jspp test suite can compile containers in
// process; this executable only handles the command line.
//
// The output bundles one quickjs bytecode blob per module plus a module table
// and an import table (see jspp-backend/BytecodeContainer.h). Bytecode embeds
// a checksum and a format version, so a container is only readable by the
// quickjs build that produced it: scriptc links the same quickjs library as
// the jspp runtime.
//
// usage: scriptc [--version] [-m] [-n name] [-M name]... -o out.jsc in.js
// relative imports must stay inside the entry file's directory tree; imports
// leaving that root, and bare or rooted specifiers, must be declared as
// native modules via -M (resolved by the host at runtime). -n overrides the
// entry module's baked name (default: the entry file's name); the override
// also becomes the base for the entry's relative imports.

#include "scriptc_core.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "quickjs.h"

namespace {

void printUsage() {
    std::fprintf(stderr, "usage: scriptc [--version] [-m] [-n name] [-M name]... -o out.jsc in.js\n");
}

// Write the container bytes straight into the output file. Compilation has
// already finished and succeeded at this point; a disk failure during this
// write may leave a truncated output, which is the accepted semantics of a
// direct overwrite. A compilation failure never reaches this function, so a
// previous good output survives it.
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
        std::fprintf(stderr, "Could not write '%s'\n", outFilename.c_str());
    }
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    int         optind = 1;
    const char* outFilename = nullptr;
    const char* inFilename  = nullptr;
    const char* entryName   = nullptr;
    bool        forceModule = false;
    std::vector<std::string> nativeModules;

    while (optind < argc) {
        const char* arg = argv[optind];
        if (std::strcmp(arg, "--version") == 0) {
            std::printf("scriptc (quickjs %d.%d.%d)\n", QJS_VERSION_MAJOR, QJS_VERSION_MINOR, QJS_VERSION_PATCH);
            return 0;
        } else if (std::strcmp(arg, "-o") == 0) {
            if (++optind >= argc) {
                std::fprintf(stderr, "scriptc: missing argument for -o\n");
                return 1;
            }
            outFilename = argv[optind++];
        } else if (std::strcmp(arg, "-M") == 0) {
            if (++optind >= argc) {
                std::fprintf(stderr, "scriptc: missing argument for -M\n");
                return 1;
            }
            nativeModules.emplace_back(argv[optind++]);
        } else if (std::strcmp(arg, "-n") == 0) {
            if (++optind >= argc) {
                std::fprintf(stderr, "scriptc: missing argument for -n\n");
                return 1;
            }
            entryName = argv[optind++];
        } else if (std::strcmp(arg, "-m") == 0) {
            // force module mode; JS_DetectModule may misclassify files whose
            // first import is relative (same as qjsc)
            forceModule = true;
            optind++;
        } else {
            if (inFilename) {
                std::fprintf(stderr, "scriptc: expected a single input file\n");
                return 1;
            }
            inFilename = argv[optind++];
        }
    }
    if (!outFilename || !inFilename) {
        printUsage();
        return 1;
    }
    if (entryName && *entryName == '\0') {
        // an explicit empty override is a build misconfiguration, not a
        // request for the default name
        std::fprintf(stderr, "scriptc: -n requires a non-empty module name\n");
        return 1;
    }

    try {
        // module names are relative to the entry file's directory: the
        // compilation root is where the entry file lives
        auto entryPath = std::filesystem::weakly_canonical(inFilename);
        auto rootDir   = entryPath.parent_path();
        auto fileName  = entryPath.filename().string();

        // -n overrides only the baked module name; the source is still read
        // from the input file's own path
        auto bytes = jspp::scriptc::compileModuleContainer(
            rootDir,
            fileName,
            nativeModules,
            forceModule,
            entryName ? std::optional<std::string>{entryName} : std::nullopt
        );

        if (!writeOutput(outFilename, bytes)) {
            return 1;
        }
        return 0;
    } catch (std::exception const& e) {
        // compilation failed before anything was written: the previous output
        // file, if any, must survive untouched
        std::fprintf(stderr, "scriptc: %s\n", e.what());
        return 1;
    }
}
