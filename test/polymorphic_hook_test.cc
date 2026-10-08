#include "catch2/catch_test_macros.hpp"

#include "jspp/binding/MetaBuilder.h"
#include "jspp/binding/ReturnValuePolicy.h"
#include "jspp/binding/TypeConverter.h"
#include "jspp/binding/traits/Polymorphic.h"
#include "jspp/binding/traits/TypeTraits.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"

#include <memory>
#include <type_traits>
#include <typeinfo>

namespace {

using namespace jspp;
using namespace jspp::binding;

// ============================================================================
// Test types
// ============================================================================

struct RttiFreeOrigin {
    virtual ~RttiFreeOrigin() = default;
    virtual int kind() const { return 0; }
};

struct RttiFreeDerived : RttiFreeOrigin {
    int kind() const override { return 1; }
};

} // namespace

/// 声明该类型 RTTI 不可用, 按静态类型判定
namespace jspp::binding::traits {

template <>
struct PolymorphicTypeHook<RttiFreeOrigin> {
    static const void* get(RttiFreeOrigin const* src, std::type_info const*& type) {
        type = nullptr;
        return src;
    }
};

} // namespace jspp::binding::traits

namespace {

// 定制点只按裸类型生效: const 修饰是另一个模板实参, 不会命中上面的显式特化
static_assert(!std::is_same_v<jspp::binding::traits::PolymorphicTypeHook<RttiFreeOrigin const>,
                              jspp::binding::traits::PolymorphicTypeHook<RttiFreeOrigin>>);
static_assert(std::is_same_v<jspp::binding::traits::RawType_t<RttiFreeOrigin const>, RttiFreeOrigin>);

static auto RttiFreeOriginMeta = binding::defClass<RttiFreeOrigin>("RttiFreeOrigin").ctor(nullptr).build();

static auto RttiFreeDerivedMeta = binding::defClass<RttiFreeDerived>("RttiFreeDerived").ctor(nullptr).build();

struct PolymorphicHookFixture {
    std::unique_ptr<Engine> engine;
    RttiFreeDerived         derived;

    PolymorphicHookFixture() : engine(std::make_unique<Engine>()) {
        EngineScope scope{engine.get()};
        engine->registerClass(RttiFreeOriginMeta);
        engine->registerClass(RttiFreeDerivedMeta);
    }
};

// ============================================================================
// Tests
// ============================================================================

TEST_CASE_METHOD(PolymorphicHookFixture, "PolymorphicTypeHook: hook wins over RTTI downcast") {
    EngineScope scope{engine.get()};

    // 裸类型: 命中定制点 -> 不做 RTTI 下转, meta 落在静态类型上
    auto plain = traits::detail::resolveCastSource<RttiFreeOrigin>(&derived);
    CHECK(plain.is_downcasted == false);
    CHECK(plain.meta == &RttiFreeOriginMeta);
}

TEST_CASE_METHOD(PolymorphicHookFixture, "PolymorphicTypeHook: const-qualified static type hits the same hook") {
    EngineScope scope{engine.get()};

    // const 静态类型: 修复前按 const RttiFreeOrigin 查定制点, 落到主模板 -> typeid/dynamic_cast 下转到派生类型
    auto asConst = traits::detail::resolveCastSource<RttiFreeOrigin const>(&derived);
    CHECK(asConst.is_downcasted == false);
    CHECK(asConst.meta == &RttiFreeOriginMeta);
    CHECK(asConst.ptr == static_cast<void const*>(&derived));
}

TEST_CASE_METHOD(PolymorphicHookFixture, "PolymorphicTypeHook: toJs of const reference keeps static type") {
    EngineScope scope{engine.get()};

    RttiFreeOrigin const& ref = derived;
    auto                  js  = binding::toJs(ref, ReturnValuePolicy::kReference, {});

    auto payload = engine->getInstancePayload(js.asObject());
    REQUIRE(payload != nullptr);

    // 承载实例的 meta 是静态类型, 不是 RTTI 下的派生类型; const 语义照样保留
    CHECK(payload->getHolder().meta() == &RttiFreeOriginMeta);
    CHECK(payload->getHolder().is_const() == true);
}

} // namespace
