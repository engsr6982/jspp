#include "catch2/matchers/catch_matchers_string.hpp"
#include "jspp/Jspp.h"
#include "jspp/binding/TypeConverter.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Trampoline.h"
#include "jspp/core/Value.h"

#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/MetaBuilder.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/generators/catch_generators.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_exception.hpp"
#include "jspp/core/Utils.h"

#include <iostream>

namespace {

namespace ns = jspp::namespace_utils;

TEST_CASE("ClassMetaBuilder: valid names", "[ClassMetaBuilder]") {
    auto names = GENERATE("ClassName", "a.Class", "a.b.c.ClassName", "_MyClass", "abc123");
    REQUIRE_NOTHROW(jspp::binding::defClass<void>(names).build());
}

TEST_CASE("ClassMetaBuilder: invalid names", "[ClassMetaBuilder]") {
    auto names = GENERATE("", ".Class", "Class.", "a..Class", ".", "..");
    REQUIRE_THROWS_AS(jspp::binding::defClass<void>(names).build(), std::invalid_argument);
}


struct Utils {
    static int add(int a, int b) { return a + b; }
};
auto const UtilsMeta = jspp::binding::defClass<void>("com.example.Utils").func("add", &Utils::add).build();

TEST_CASE("ClassMetaBuilder: script test", "[ClassMetaBuilder]") {
    auto              engine = std::make_unique<jspp::Engine>();
    jspp::EngineScope lock{engine.get()};

    engine->registerClass(UtilsMeta);

    auto result = engine->evalScript(jspp::String::newString("com.example.Utils.add(1, 2)"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == 3);
}

} // namespace