#include "catch2/matchers/catch_matchers_string.hpp"
#include "jspp/Jspp.h"
#include "jspp/binding/ReturnValuePolicy.h"
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
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_exception.hpp"

#include <iostream>
#include <memory>
#include <optional>

namespace {

using namespace jspp;

struct BugTestFixture {
    std::unique_ptr<jspp::Engine> engine;

    BugTestFixture() : engine(std::make_unique<jspp::Engine>()) {}
};

struct Color {
    int r, g, b, a;

    static Color const& RED() {
        static constexpr auto red = Color{255, 0, 0, 255};
        return red;
    }
};
class Shape {
public:
    Shape()             = default;
    using OptionalColor = std::optional<Color>;
    OptionalColor color;

    OptionalColor getColor() const { return color; }
    void          setColor(OptionalColor c) { color = c; }
};

TEST_CASE_METHOD(
    BugTestFixture,
    "Bug: TypeConverter value types require mutable pointer, rejecting const instances (e.g. Color.RED)",
    "[bugs]"
) {
    static auto colorMeta = jspp::binding::defClass<Color>("Color")
                                .ctor(nullptr)
                                .prop("r", &Color::r)
                                .prop("g", &Color::g)
                                .prop("b", &Color::b)
                                .prop("a", &Color::a)
                                .var_readonly("RED", &Color::RED, binding::ReturnValuePolicy::kReferencePersistent)
                                .build();
    static auto shapeMeta =
        jspp::binding::defClass<Shape>("Shape").ctor<>().prop("color", &Shape::getColor, &Shape::setColor).build();

    EngineScope lock{*engine};

    engine->registerClass(colorMeta);
    engine->registerClass(shapeMeta);

    REQUIRE_NOTHROW(engine->evalScript(String::newString("new Shape().color = Color.RED")));
}


struct Base {
    int       foo;
    int const bar = 42;
    Base(int foo) : foo(foo) {}
};
struct Derived : public Base {
    using Base::Base;
};
TEST_CASE_METHOD(
    BugTestFixture,
    "Bug: member pointer from derived class fails unwrap due to base class type mismatch",
    "[bugs]"
) {
    static auto Test =
        binding::defClass<Derived>("Derived").ctor<int>().prop("foo", &Derived::foo).prop("bar", &Base::bar).build();

    EngineScope lock{*engine};
    engine->registerClass(Test);

    REQUIRE_NOTHROW(engine->evalScript(String::newString("new Derived(1).foo = 42")));
    REQUIRE_THROWS(engine->evalScript(String::newString("new Derived(1).bar = 42")));
}


} // namespace
