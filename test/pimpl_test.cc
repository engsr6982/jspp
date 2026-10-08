#include "jspp/Jspp.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/InstancePayload.h"
#include "jspp/core/MetaInfo.h"

#include "catch2/catch_test_macros.hpp"

#include "pimpl/pimpl_target.h"

namespace {

using namespace jspp;
using namespace jspp::binding;

static auto PimplTargetMeta = defClass<PimplTarget>("PimplTarget")
                                  .ctor(nullptr)  // 实例类, 但脚本不能构造
                                  .method("value", &PimplTarget::value)
                                  .method("setValue", &PimplTarget::setValue)
                                  .build();

} // namespace

namespace jspp::binding::traits {

// PimplTarget 的析构需要 Impl 的定义, 而这个 TU 看不到 Impl。
// 声明成"只按引用暴露"后, jspp 不再为它实例化所有权路径。
template <>
constexpr bool isReferenceOnlyType<PimplTarget> = true;

} // namespace jspp::binding::traits

namespace {

TEST_CASE("pImpl type is exposed as a reference without seeing Impl") {
    auto        engine = std::make_unique<Engine>();
    EngineScope enter{engine.get()};
    engine->registerClass(PimplTargetMeta);

    auto& target = pimplTarget();
    target.setValue(42);

    // 这个 TU 看不到 PimplTarget::Impl, 转成 JS 包装器时不能触发
    // 任何需要完整类型的所有权操作（拷贝 / 析构）
    engine->globalThis().set(String::newString("target"), toJs<PimplTarget&>(target));

    auto wrapper = engine->globalThis().get(String::newString("target")).asObject();
    auto payload = engine->getInstancePayload(wrapper);
    REQUIRE(payload != nullptr);

    // 非拥有引用：不接管所有权，类型信息来自静态类型
    CHECK(payload->getHolder().is_owned() == false);
    CHECK(payload->getHolder().meta() == &PimplTargetMeta);

    // 方法调用与写回
    CHECK(engine->evalScript(String::newString("target.value()")).asNumber().getInt32() == 42);
    engine->evalScript(String::newString("target.setValue(7)"));
    CHECK(target.value() == 7);

    // 包装器失效后访问应该报受控异常
    payload->getHolder().invalidate();
    REQUIRE_THROWS_AS(engine->evalScript(String::newString("target.value()")), Exception);
}

} // namespace
