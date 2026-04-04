#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"

#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers.hpp"
#include "catch2/matchers/catch_matchers_exception.hpp"

struct CoreTestFixture {
    std::unique_ptr<jspp::Engine> engine;
    CoreTestFixture() { engine = std::make_unique<jspp::Engine>(); }
};

TEST_CASE_METHOD(CoreTestFixture, "Engine::eval") {
    REQUIRE(engine != nullptr);

    jspp::EngineScope scope(engine.get());

    auto result = engine->eval(jspp::String::newString("1 + 1"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == 2);

    result = engine->eval(jspp::String::newString("1 + '1'"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "11");
}


class ScriptClass {
public:
    static jspp::Local<jspp::Value> foo(jspp::Arguments const& arguments) { return jspp::String::newString("foo"); }
    static jspp::Local<jspp::Value> forward(jspp::Arguments const& arguments) { return arguments[0]; }

    inline static std::string       name{"123"};
    static jspp::Local<jspp::Value> getter() { return jspp::String::newString(name); }
    static void                     setter(jspp::Local<jspp::Value> const& value) {
        if (value.isString()) {
            name = value.asString().getValue();
        }
    }
};
TEST_CASE_METHOD(CoreTestFixture, "registerClass") {
    jspp::EngineScope scope{engine.get()};

    // clang-format off
    static auto meta = jspp::ClassMeta{
        "ScriptClass",
        jspp::StaticMemberMeta{
            {
                jspp::StaticMemberMeta::Property{"name", &ScriptClass::getter, &ScriptClass::setter},
            },
            {
                jspp::StaticMemberMeta::Function{"foo", &ScriptClass::foo},
                jspp::StaticMemberMeta::Function{"forward", &ScriptClass::forward}
            },
        },
        jspp::InstanceMemberMeta{
            nullptr,
            {},
            {},
            sizeof(ScriptClass),
            nullptr
        },
        nullptr,
        typeid(ScriptClass)
    };
    // clang-format on

    engine->registerClass(meta);

    auto result = engine->eval(jspp::String::newString("ScriptClass.foo()"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "foo");

    result = engine->eval(jspp::String::newString("ScriptClass.forward('bar')"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "bar");

    engine->eval(jspp::String::newString("ScriptClass.name = 'bar'"));
    result = engine->eval(jspp::String::newString("ScriptClass.name"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "bar");

    // ensure toStringTag
    result = engine->eval(jspp::String::newString("Object.prototype.toString.call(ScriptClass)"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "[object ScriptClass]");
}


enum class Color { Red, Green, Blue };
TEST_CASE_METHOD(CoreTestFixture, "registerEnum") {
    jspp::EngineScope scope{engine.get()};

    static auto meta = jspp::EnumMeta{
        "Color",
        {
          jspp::EnumMeta::Entry{"Red", static_cast<int64_t>(Color::Red)},
          jspp::EnumMeta::Entry{"Green", static_cast<int64_t>(Color::Green)},
          jspp::EnumMeta::Entry{"Blue", static_cast<int64_t>(Color::Blue)},
          }
    };

    engine->registerEnum(meta);

    auto result = engine->eval(jspp::String::newString("Color.$name"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "Color");

    result = engine->eval(jspp::String::newString("Color.Red"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == static_cast<int64_t>(Color::Red));

    result = engine->eval(jspp::String::newString("Color.Green"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == static_cast<int64_t>(Color::Green));

    result = engine->eval(jspp::String::newString("Color.Blue"));
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == static_cast<int64_t>(Color::Blue));

    // ensure toStringTag
    result = engine->eval(jspp::String::newString("Object.prototype.toString.call(Color)"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "[object Color]");

    // ensure $name don't enumerate
    auto ensure = jspp::Function::newFunction([](jspp::Arguments const& arguments) -> jspp::Local<jspp::Value> {
        REQUIRE(arguments.length() == 1);
        REQUIRE(arguments[0].isString());
        REQUIRE(arguments[0].asString().getValue() != "$name");
        return {};
    });
    engine->globalThis().set(jspp::String::newString("ensure"), ensure);
    engine->eval(jspp::String::newString("for (let key in Color) { ensure(key) }"));
}


TEST_CASE_METHOD(CoreTestFixture, "Exception pass-through") {
    jspp::EngineScope scope{engine.get()};

    REQUIRE_THROWS_MATCHES(
        engine->eval(jspp::String::newString("throw new Error('abc')")),
        jspp::Exception,
        Catch::Matchers::Message("Uncaught Error: abc")
    );

    static constexpr auto msg = "Cpp layer throw exception";
    auto thowr  = jspp::Function::newFunction([](jspp::Arguments const& arguments) -> jspp::Local<jspp::Value> {
        throw jspp::Exception{msg};
    });
    auto ensure = jspp::Function::newFunction([](jspp::Arguments const& arguments) -> jspp::Local<jspp::Value> {
        REQUIRE(arguments.length() == 1);
        REQUIRE(arguments[0].isString());
        REQUIRE(arguments[0].asString().getValue() == msg);
        return {};
    });
    engine->globalThis().set(jspp::String::newString("throwr"), thowr);
    engine->globalThis().set(jspp::String::newString("ensure"), ensure);

    engine->eval(jspp::String::newString("try { throwr() } catch (e) { ensure(e.message) }"));
}


TEST_CASE("Local<T> via Engine::eval - Boolean") {
    using namespace jspp;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineScope             enter{engine.get()};

    auto bTrue  = engine->eval(String::newString("true"));
    auto bFalse = engine->eval(String::newString("false"));

    REQUIRE(bTrue.isBoolean());
    REQUIRE(bTrue.asBoolean().getValue() == true);

    REQUIRE(bFalse.isBoolean());
    REQUIRE(bFalse.asBoolean().getValue() == false);
}

TEST_CASE("Local<T> via Engine::eval - Number") {
    using namespace jspp;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineScope             enter{engine.get()};

    auto n = engine->eval(String::newString("42"));
    REQUIRE(n.isNumber());
    REQUIRE(n.asNumber().getInt32() == 42);
}

TEST_CASE("Local<T> via Engine::eval - String") {
    using namespace jspp;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineScope             enter{engine.get()};

    auto s = engine->eval(String::newString("'hello'"));
    REQUIRE(s.isString());
    REQUIRE(s.asString().getValue() == "hello");
    REQUIRE(s.asString().length() == 5);
}

TEST_CASE("Local<T> via Engine::eval - Null & Undefined") {
    using namespace jspp;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineScope             enter{engine.get()};

    auto n = engine->eval(String::newString("null"));
    auto u = engine->eval(String::newString("undefined"));

    REQUIRE(n.isNull());
    REQUIRE(u.isUndefined());
    REQUIRE(n.isNullOrUndefined());
    REQUIRE(u.isNullOrUndefined());
}

TEST_CASE("Local<T> via Engine::eval - BigInt") {
    using namespace jspp;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineScope             enter{engine.get()};

    auto bi = engine->eval(String::newString("1234567890123456789n"));
    REQUIRE(bi.isBigInt());
    REQUIRE(bi.asBigInt().getInt64() == 1234567890123456789LL);
}

TEST_CASE("Local<T> via Engine::eval - Symbol") {
    using namespace jspp;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineScope             enter{engine.get()};

    auto s = engine->eval(String::newString("Symbol('desc')"));
    REQUIRE(s.isSymbol());
    auto desc = s.asSymbol().getDescription();
    REQUIRE(desc.isString());
    REQUIRE(desc.asString().getValue() == "desc");
}

TEST_CASE("Local<T> via Engine::eval - Object") {
    using namespace jspp;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineScope             enter{engine.get()};

    auto obj = engine->eval(String::newString("({foo: 123, bar: 'abc'})"));
    REQUIRE(obj.isObject());

    auto foo = obj.asObject().get(String::newString("foo"));
    REQUIRE(foo.isNumber());
    REQUIRE(foo.asNumber().getInt32() == 123);

    auto bar = obj.asObject().get(String::newString("bar"));
    REQUIRE(bar.isString());
    REQUIRE(bar.asString().getValue() == "abc");

    obj.asObject().remove(String::newString("foo"));
    REQUIRE_FALSE(obj.asObject().has(String::newString("foo")));
}

TEST_CASE("Local<T> via Engine::eval - Array") {
    using namespace jspp;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineScope             enter{engine.get()};

    auto arr = engine->eval(String::newString("[1,2,3]"));
    REQUIRE(arr.isArray());
    auto a = arr.asArray();
    REQUIRE(a.length() == 3);
    REQUIRE(a.get(0).asNumber().getInt32() == 1);
    REQUIRE(a[1].asNumber().getInt32() == 2);
}

TEST_CASE("Local<T> via Engine::eval - Function") {
    using namespace jspp;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineScope             enter{engine.get()};

    auto fn = engine->eval(String::newString("(function(x){return x+1;})"));
    REQUIRE(fn.isFunction());
    auto f = fn.asFunction();

    auto result = f.call(engine->globalThis(), {Number::newNumber(41)});
    REQUIRE(result.isNumber());
    REQUIRE(result.asNumber().getInt32() == 42);

    auto value =
        engine->eval(String::newString("class Foo { constructor(x){this.x = x;}  getX() {return this.x;} };Foo"));
    REQUIRE(value.isFunction());
    auto ctor = value.asFunction();
    auto foo  = ctor.callAsConstructor({Number::newNumber(42)});
    REQUIRE(foo.isObject());
    auto fooObj = foo.asObject();
    auto _getX  = fooObj.get(String::newString("getX"));
    REQUIRE(_getX.isFunction());
    auto getX = _getX.asFunction();
    auto x    = getX.call(foo, {});
    REQUIRE(x.isNumber());
    REQUIRE(x.asNumber().getInt32() == 42);
}

TEST_CASE("Local<T> via Engine::eval - as<T> conversion") {
    using namespace jspp;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineScope             enter{engine.get()};

    auto          n   = engine->eval(String::newString("99"));
    Local<Value>  v   = n.asValue();
    Local<Number> num = v.as<Number>();
    REQUIRE(num.getInt32() == 99);
}

TEST_CASE("Local<T> via Engine::eval - operator== and clear") {
    using namespace jspp;
    std::unique_ptr<Engine> engine = std::make_unique<Engine>();
    EngineScope             enter{engine.get()};

    auto n1 = engine->eval(String::newString("10"));
    auto n2 = engine->eval(String::newString("10"));

    REQUIRE(n1 == n2.asValue());
    n1.clear();
    REQUIRE_FALSE(n1.isNumber());
}