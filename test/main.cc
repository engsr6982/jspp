#include "jspp/Macro.h"

#include <catch2/catch_session.hpp>

JSPP_WARNING_GUARD_BEGIN
#include <libplatform/libplatform-export.h>
#include <libplatform/libplatform.h>
#include <libplatform/v8-tracing.h>
#include <v8-initialization.h>
#include <v8-platform.h>
JSPP_WARNING_GUARD_END

int main(int argc, char* argv[]) {
    auto plat = v8::platform::NewDefaultPlatform(1);
    v8::V8::InitializePlatform(plat.get());
    v8::V8::Initialize();

    int code = Catch::Session().run(argc, argv);

    v8::V8::Dispose();
    v8::V8::DisposePlatform();
    return code;
}
