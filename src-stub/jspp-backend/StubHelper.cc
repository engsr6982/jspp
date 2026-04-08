#include "StubHelper.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

namespace jspp::stub_backend {

void StubHelper::rethrowToScript(Exception const& exception) {
    // TODO: please implement this
    // Need to convert C exceptions into script exceptions
}


} // namespace jspp::stub_backend