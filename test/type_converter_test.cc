#include "jspp/Jspp.h"
#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/binding/TypeConverter.h"


#include "catch2/catch_test_macros.hpp"
#include <catch2/catch_approx.hpp>

#include <cmath>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

TEST_CASE("TypeConverter full test") {
    auto              engine = std::make_unique<jspp::Engine>();
    jspp::EngineScope enter{engine.get()};

    using namespace jspp::binding;

    // ----------------------------
    // bool
    // ----------------------------
    auto js_bool = toJs(true);
    REQUIRE(js_bool.isBoolean());
    REQUIRE(js_bool.asBoolean().getValue() == true);

    bool cpp_bool = toCpp<bool>(js_bool);
    REQUIRE(cpp_bool == true);

    // ----------------------------
    // numbers
    // ----------------------------
    int32_t n_i32  = 123;
    auto    js_i32 = toJs(n_i32);
    REQUIRE(js_i32.isNumber());
    REQUIRE(js_i32.asNumber().getInt32() == n_i32);

    double n_double  = 3.14;
    auto   js_double = toJs(n_double);
    REQUIRE(js_double.isNumber());
    REQUIRE(js_double.asNumber().getValueAs<double>() == Catch::Approx(3.14));

    int64_t n_i64  = 9876543210;
    auto    js_i64 = toJs(n_i64);
    REQUIRE(js_i64.isBigInt());
    REQUIRE(js_i64.asBigInt().getInt64() == n_i64);

    uint64_t n_u64  = 1234567890;
    auto     js_u64 = toJs(n_u64);
    REQUIRE(js_u64.isBigInt());
    REQUIRE(js_u64.asBigInt().getUint64() == n_u64);

    // ----------------------------
    // strings
    // ----------------------------
    std::string str    = "hello";
    auto        js_str = toJs(str);
    REQUIRE(js_str.isString());
    REQUIRE(js_str.asString().getValue() == str);

    // char array
    const char* cstr    = "world";
    auto        js_cstr = toJs(cstr);
    REQUIRE(js_cstr.isString());
    REQUIRE(js_cstr.asString().getValue() == cstr);

    // ----------------------------
    // enum
    // ----------------------------
    enum class Color { Red, Green, Blue };
    auto js_enum = toJs(Color::Green);
    REQUIRE(js_enum.isNumber());
    REQUIRE(js_enum.asNumber().getInt32() == static_cast<int>(Color::Green));

    Color cpp_enum = toCpp<Color>(js_enum);
    REQUIRE(cpp_enum == Color::Green);

    // ----------------------------
    // optional
    // ----------------------------
    std::optional<int> opt    = std::nullopt;
    auto               js_opt = toJs(opt);
    REQUIRE(js_opt.isNull());

    opt    = 42;
    js_opt = toJs(opt);
    REQUIRE(js_opt.isNumber());
    REQUIRE(js_opt.asNumber().getInt32() == 42);

    std::optional<int> cpp_opt = toCpp<std::optional<int>>(js_opt);
    REQUIRE(cpp_opt.has_value());
    REQUIRE(cpp_opt.value() == 42);

    // ----------------------------
    // vector
    // ----------------------------
    std::vector<int> vec    = {1, 2, 3};
    auto             js_vec = toJs(vec);
    REQUIRE(js_vec.isArray());
    REQUIRE(js_vec.asArray().length() == 3);
    REQUIRE(js_vec.asArray().get(0).asNumber().getInt32() == 1);
    REQUIRE(js_vec.asArray().get(1).asNumber().getInt32() == 2);
    REQUIRE(js_vec.asArray().get(2).asNumber().getInt32() == 3);

    std::vector<int> cpp_vec = toCpp<std::vector<int>>(js_vec);
    REQUIRE(cpp_vec == vec);

    // ----------------------------
    // unordered_map
    // ----------------------------
    std::unordered_map<std::string, int> map = {
        {"a", 1},
        {"b", 2}
    };
    auto js_map = toJs(map);
    REQUIRE(js_map.isObject());

    auto cpp_map = toCpp<std::unordered_map<std::string, int>>(js_map);
    REQUIRE(cpp_map == map);

    // ----------------------------
    // pair
    // ----------------------------
    std::pair<int, std::string> p       = {42, "pair"};
    auto                        js_pair = toJs(p);
    REQUIRE(js_pair.isArray());
    REQUIRE(js_pair.asArray().length() == 2);
    REQUIRE(js_pair.asArray().get(0).asNumber().getInt32() == 42);
    REQUIRE(js_pair.asArray().get(1).asString().getValue() == "pair");

    auto cpp_pair = toCpp<std::pair<int, std::string>>(js_pair);
    REQUIRE(cpp_pair == p);

    // ----------------------------
    // variant
    // ----------------------------
    std::variant<int, std::string> var    = 123;
    auto                           js_var = toJs(var);
    REQUIRE(js_var.isNumber());

    auto cpp_var = toCpp<std::variant<int, std::string>>(js_var);
    REQUIRE(std::get<int>(cpp_var) == 123);

    var    = std::string("variant");
    js_var = toJs(var);
    REQUIRE(js_var.isString());
    cpp_var = toCpp<std::variant<int, std::string>>(js_var);
    REQUIRE(std::get<std::string>(cpp_var) == "variant");

    // ----------------------------
    // monostate
    // ----------------------------
    std::monostate ms;
    auto           js_ms = toJs(ms);
    REQUIRE(js_ms.isNull());

    auto cpp_ms = toCpp<std::monostate>(js_ms);
    (void)cpp_ms; // just type check

    // ----------------------------
    // nested containers
    // ----------------------------
    std::vector<std::optional<int>> nested    = {1, std::nullopt, 3};
    auto                            js_nested = toJs(nested);
    REQUIRE(js_nested.isArray());
    auto cpp_nested = toCpp<std::vector<std::optional<int>>>(js_nested);
    REQUIRE(cpp_nested.size() == 3);
    REQUIRE(cpp_nested[0] == 1);
    REQUIRE(!cpp_nested[1].has_value());
    REQUIRE(cpp_nested[2] == 3);
}


TEST_CASE("Numeric conversion safety") {
    auto              engine = std::make_unique<jspp::Engine>();
    jspp::EngineScope enter{engine.get()};

    using namespace jspp;
    using namespace jspp::binding;

    auto num = [](double d) { return Number::newNumber(d); };
    auto big = [&](std::string_view src) { return engine->evalScript(String::newString(src)); };

    SECTION("negative values are rejected for unsigned targets") {
        REQUIRE_THROWS_AS(toCpp<uint32_t>(num(-1)), jspp::Exception);
        REQUIRE_THROWS_AS(toCpp<uint64_t>(num(-1)), jspp::Exception);
        REQUIRE_THROWS_AS(toCpp<uint8_t>(num(-1)), jspp::Exception);
        REQUIRE(toCpp<unsigned>(num(-0.5)) == 0u); // 截断后是 0, 与 C++ 隐式转换一致

        // 有符号目标照常接受
        REQUIRE(toCpp<int>(num(-1)) == -1);
        REQUIRE(toCpp<int64_t>(num(-1)) == -1);
        REQUIRE(toCpp<double>(num(-1)) == -1.0);
    }

    SECTION("plain numbers convert to 64-bit targets") {
        // 脚本给 1, C++ 要 uint64_t / int64_t: 走隐式转换, 不需要写 1n
        REQUIRE(toCpp<uint64_t>(num(1)) == 1);
        REQUIRE(toCpp<int64_t>(num(1)) == 1);
        REQUIRE(toCpp<int64_t>(num(-1)) == -1);
        REQUIRE(toCpp<uint64_t>(toJs<uint64_t>(12345)) == 12345);
    }

    SECTION("fractional values follow C++ implicit conversion (truncate)") {
        REQUIRE(toCpp<int>(num(1.5)) == 1);
        REQUIRE(toCpp<int>(num(-1.5)) == -1);
        REQUIRE(toCpp<int8_t>(num(1.999)) == 1);
        REQUIRE(toCpp<uint32_t>(num(-0.5)) == 0);
        REQUIRE_THROWS_AS(toCpp<uint32_t>(num(-1.5)), jspp::Exception);

        // 截断之后再判范围
        REQUIRE(toCpp<uint8_t>(num(255.9)) == 255);
        REQUIRE_THROWS_AS(toCpp<uint8_t>(num(256.0)), jspp::Exception);

        // 浮点目标照常接受
        REQUIRE(toCpp<double>(num(1.5)) == Catch::Approx(1.5));
        REQUIRE(toCpp<float>(num(1.5)) == Catch::Approx(1.5f));
    }

    SECTION("NaN and Infinity are rejected for integral targets") {
        REQUIRE_THROWS_AS(toCpp<int>(num(std::nan(""))), jspp::Exception);
        REQUIRE_THROWS_AS(toCpp<int>(num(std::numeric_limits<double>::infinity())), jspp::Exception);
        REQUIRE_THROWS_AS(toCpp<uint64_t>(num(-std::numeric_limits<double>::infinity())), jspp::Exception);
        REQUIRE(std::isnan(toCpp<double>(num(std::nan("")))));
    }

    SECTION("number range boundaries") {
        REQUIRE(toCpp<int8_t>(num(-128)) == -128);
        REQUIRE(toCpp<int8_t>(num(127)) == 127);
        REQUIRE_THROWS_AS(toCpp<int8_t>(num(-129)), jspp::Exception);
        REQUIRE_THROWS_AS(toCpp<int8_t>(num(128)), jspp::Exception);

        REQUIRE(toCpp<uint8_t>(num(0)) == 0);
        REQUIRE(toCpp<uint8_t>(num(255)) == 255);
        REQUIRE_THROWS_AS(toCpp<uint8_t>(num(256)), jspp::Exception);

        REQUIRE(toCpp<int32_t>(num(2147483647.0)) == 2147483647);
        REQUIRE_THROWS_AS(toCpp<int32_t>(num(2147483648.0)), jspp::Exception);
        REQUIRE(toCpp<uint32_t>(num(4294967295.0)) == 4294967295u);
        REQUIRE_THROWS_AS(toCpp<uint32_t>(num(4294967296.0)), jspp::Exception);

        // 2^63 / 2^64 在 double 里都是精确值, 必须落在对应类型的边界外
        REQUIRE(toCpp<uint64_t>(num(9223372036854775808.0)) == 9223372036854775808ull);
        REQUIRE_THROWS_AS(toCpp<int64_t>(num(9223372036854775808.0)), jspp::Exception);
        REQUIRE(toCpp<int64_t>(num(-9223372036854775808.0)) == std::numeric_limits<int64_t>::min());
        REQUIRE_THROWS_AS(toCpp<uint64_t>(num(18446744073709551616.0)), jspp::Exception);
    }

    SECTION("bigint range boundaries") {
        REQUIRE(toCpp<int64_t>(big("-9223372036854775808n")) == std::numeric_limits<int64_t>::min());
        REQUIRE(toCpp<int64_t>(big("9223372036854775807n")) == std::numeric_limits<int64_t>::max());
        REQUIRE_THROWS_AS(toCpp<int64_t>(big("9223372036854775808n")), jspp::Exception);
        REQUIRE_THROWS_AS(toCpp<int64_t>(big("-9223372036854775809n")), jspp::Exception);

        REQUIRE(toCpp<uint64_t>(big("18446744073709551615n")) == std::numeric_limits<uint64_t>::max());
        REQUIRE_THROWS_AS(toCpp<uint64_t>(big("18446744073709551616n")), jspp::Exception);
        REQUIRE_THROWS_AS(toCpp<uint64_t>(big("-1n")), jspp::Exception);
        REQUIRE_THROWS_AS(toCpp<uint32_t>(big("4294967296n")), jspp::Exception);

        REQUIRE(toCpp<int32_t>(big("-2147483648n")) == std::numeric_limits<int32_t>::min());
        REQUIRE(toCpp<uint32_t>(big("4294967295n")) == std::numeric_limits<uint32_t>::max());

        // BigInt 不往浮点参数上转
        REQUIRE_THROWS_AS(toCpp<double>(big("1n")), jspp::Exception);
    }

    SECTION("toJs keeps the sign of 64-bit integers") {
        auto bigU = toJs<uint64_t>(18446744073709551615ull);
        REQUIRE(bigU.isBigInt());
        REQUIRE(bigU.asBigInt().getUint64() == 18446744073709551615ull);
        REQUIRE(toCpp<uint64_t>(bigU) == 18446744073709551615ull);

        auto bigI = toJs<int64_t>(-1);
        REQUIRE(bigI.isBigInt());
        REQUIRE(bigI.asBigInt().getInt64() == -1);
        REQUIRE_THROWS_AS(toCpp<uint64_t>(bigI), jspp::Exception);

        // 32 位整数仍然走 Number
        REQUIRE(toJs<uint32_t>(4294967295u).isNumber());
        REQUIRE(toCpp<uint32_t>(toJs<uint32_t>(4294967295u)) == 4294967295u);
    }
}

// ----------------------------------------------------------------------------
// std::function: null/undefined -> empty callback
// ----------------------------------------------------------------------------
namespace {

struct FormLike {
    std::string              lastText;
    std::function<void(int)> lastCallback;

    FormLike& append(std::string const& text, std::function<void(int)> cb) {
        lastText     = text;
        lastCallback = std::move(cb);
        return *this;
    }
    FormLike& append(std::string const& a, std::string const& b, std::string const& c, std::function<void(int)> cb) {
        lastText     = a + b + c;
        lastCallback = std::move(cb);
        return *this;
    }

    std::function<void(int)> const& callback() const { return lastCallback; }
};

FormLike formInstance;

auto FormLikeMeta = jspp::binding::defClass<FormLike>("FormLike")
                        .ctor(nullptr)
                        .method(
                            "append",
                            static_cast<FormLike& (FormLike::*)(std::string const&, std::function<void(int)>)>(
                                &FormLike::append
                            ),
                            static_cast<FormLike& (FormLike::*)(std::string const&,
                                                                std::string const&,
                                                                std::string const&,
                                                                std::function<void(int)>)>(&FormLike::append)
                        )
                        .method("callback", &FormLike::callback)
                        .build();

} // namespace

TEST_CASE("std::function accepts null/undefined as empty callback") {
    auto              engine = std::make_unique<jspp::Engine>();
    jspp::EngineScope enter{engine.get()};

    using namespace jspp::binding;

    // 直接转换: null / undefined -> 空回调
    CHECK(!toCpp<std::function<void(int)>>(jspp::Null::newNull()));
    CHECK(!toCpp<std::function<void(int)>>(jspp::Undefined::newUndefined()));

    // 反向: 空回调 -> null
    CHECK(toJs(std::function<void(int)>{}).isNull());

    engine->registerClass(FormLikeMeta);
    engine->globalThis().set(
        jspp::String::newString("getForm"),
        jspp::Function::newFunction(
            cpp_func([]() -> FormLike& { return formInstance; }, ReturnValuePolicy::kReferencePersistent)
        )
    );

    // 重载分派: append('a', null) 命中 2 参数重载。（修复前抛 "expected function", 最终报 "no overload found"）
    REQUIRE_NOTHROW(engine->evalScript(jspp::String::newString("getForm().append('a', null)")));
    CHECK(formInstance.lastText == "a");
    CHECK(!formInstance.lastCallback);

    // 4 参数重载同样接受 undefined
    REQUIRE_NOTHROW(engine->evalScript(jspp::String::newString("getForm().append('x', 'y', 'z', undefined)")));
    CHECK(formInstance.lastText == "xyz");
    CHECK(!formInstance.lastCallback);

    // 往返: 函数读回来仍是函数, 空回调读回来是 null
    engine->evalScript(jspp::String::newString("getForm().append('b', (v) => {})"));
    CHECK(formInstance.lastCallback != nullptr);
    CHECK(
        engine->evalScript(jspp::String::newString("typeof getForm().callback() === 'function'")).asBoolean().getValue()
    );

    engine->evalScript(jspp::String::newString("getForm().append('c', null)"));
    CHECK(engine->evalScript(jspp::String::newString("getForm().callback() === null")).asBoolean().getValue());
}

// 自动策略的推导模型（全部在编译期断言）
namespace {

struct CopyableOnly {
    int value{0};
};

struct MoveOnly {
    MoveOnly()                           = default;
    MoveOnly(MoveOnly const&)            = delete;
    MoveOnly& operator=(MoveOnly const&) = delete;
    MoveOnly(MoveOnly&&)                 = default;
};

using P = jspp::binding::ReturnValuePolicy;
using jspp::binding::detail::resolveAutomaticPolicy;

static_assert(resolveAutomaticPolicy<std::unique_ptr<CopyableOnly>>(P::kAutomatic) == P::kAutomatic);
static_assert(resolveAutomaticPolicy<std::shared_ptr<CopyableOnly>>(P::kAutomatic) == P::kAutomatic);
static_assert(resolveAutomaticPolicy<CopyableOnly*>(P::kAutomatic) == P::kReference);
static_assert(resolveAutomaticPolicy<CopyableOnly&&>(P::kAutomatic) == P::kMove);
static_assert(resolveAutomaticPolicy<CopyableOnly&>(P::kAutomatic) == P::kCopy);
static_assert(resolveAutomaticPolicy<CopyableOnly const&>(P::kAutomatic) == P::kCopy);
static_assert(resolveAutomaticPolicy<MoveOnly&>(P::kAutomatic) == P::kReference); // 不可拷贝的左值
static_assert(resolveAutomaticPolicy<CopyableOnly>(P::kAutomatic) == P::kMove);   // 可移动优先
static_assert(resolveAutomaticPolicy<MoveOnly>(P::kAutomatic) == P::kMove);

// 显式策略不会被改写
static_assert(resolveAutomaticPolicy<CopyableOnly*>(P::kTakeOwnership) == P::kTakeOwnership);
static_assert(resolveAutomaticPolicy<CopyableOnly&>(P::kReference) == P::kReference);

TEST_CASE("Automatic return value policy derivation") { SUCCEED("checked at compile time"); }

} // namespace
