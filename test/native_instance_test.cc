#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers_exception.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

#include "jspp/Jspp.h"
#include "jspp/binding/BindingUtils.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/binding/NativeInstanceImpl.h"
#include "jspp/binding/ReturnValuePolicy.h"
#include "jspp/binding/TypeConverter.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/NativeInstance.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Trampoline.h"

#include <memory>
#include <string>

namespace {

using namespace jspp;
using namespace jspp::binding;

// ============================================================================
// Test types
// ============================================================================

struct Widget {
    int value{0};

    explicit Widget(int v) : value(v) {}

    int getValue() const { return value; }
};

static auto WidgetMeta = binding::defClass<Widget>("Widget")
                             .ctor<int>()
                             .method("getValue", &Widget::getValue)
                             .prop("value", &Widget::value)
                             .build();

/// 不接管所有权的承载实例：只记住一个指针，用来观察 jspp 是否真的用了自定义工厂 / 直通。
class TrackingInstance final : public NativeInstance {
public:
    Widget* target_{nullptr};
    bool    owned_{false};

    TrackingInstance(ClassMeta const* meta, Widget* target, bool owned)
    : NativeInstance(meta),
      target_(target),
      owned_(owned) {}

    ~TrackingInstance() override {
        if (owned_) {
            delete target_;
            target_ = nullptr;
        }
    }

    bool is_expired() const override { return target_ == nullptr; }

    void invalidate() override { target_ = nullptr; }

    std::type_index type_id() const override { return typeid(Widget); }

    bool is_const() const override { return false; }

    void* cast(std::type_index target_type) const override {
        if (target_ == nullptr) {
            return nullptr;
        }
        if (target_type == typeid(Widget)) {
            return target_;
        }
        return meta_ ? meta_->castTo(target_, target_type) : nullptr;
    }

    bool is_owned() const override { return owned_; }

    void* release_ownership() override { return nullptr; }

    std::unique_ptr<NativeInstance> clone() const override {
        return std::make_unique<TrackingInstance>(meta_, target_, false); // 副本不接管所有权
    }
};

} // namespace

/// 工厂：非拥有语义改用 TrackingInstance，其余策略交给默认实现
namespace jspp::binding::traits {

template <>
struct NativeInstanceFactory<Widget> {
    template <typename V>
    static std::unique_ptr<NativeInstance>
    create(V&& value, ReturnValuePolicy policy, detail::ResolvedCastSource const& resolved) {
        if (policy == ReturnValuePolicy::kReference || policy == ReturnValuePolicy::kReferencePersistent) {
            return std::make_unique<TrackingInstance>(
                resolved.meta,
                static_cast<Widget*>(const_cast<void*>(resolved.ptr)),
                false
            );
        }
        return detail::NativeInstanceFactoryBase<Widget>::create(std::forward<V>(value), policy, resolved);
    }
};

} // namespace jspp::binding::traits

namespace {

struct FactoryTestFixture {
    std::unique_ptr<Engine> engine;
    Widget                  widget{7};

    FactoryTestFixture() : engine(std::make_unique<Engine>()) {
        EngineScope scope{engine.get()};
        engine->registerClass(WidgetMeta);
        engine->globalThis().set(
            String::newString("getWidgetRef"),
            Function::newFunction(binding::cpp_func([this]() -> Widget& { return widget; },
                                                    ReturnValuePolicy::kReferencePersistent))
        );
        engine->globalThis().set(
            String::newString("makeWidget"),
            Function::newFunction(binding::cpp_func([](int v) -> std::unique_ptr<TrackingInstance> {
                return std::make_unique<TrackingInstance>(&WidgetMeta, new Widget{v}, true);
            }))
        );
    }
};

// ============================================================================
// Tests
// ============================================================================

TEST_CASE_METHOD(FactoryTestFixture, "NativeInstanceFactory: specialization is used") {
    EngineScope scope{engine.get()};

    // kReferencePersistent 走工厂特化 -> TrackingInstance
    auto wrapper = engine->evalScript(String::newString("getWidgetRef()")).asObject();
    auto payload = engine->getInstancePayload(wrapper);
    REQUIRE(payload != nullptr);
    REQUIRE(dynamic_cast<TrackingInstance*>(&payload->getHolder()) != nullptr);
    CHECK(payload->getHolder().is_owned() == false);

    // 绑定在 Widget 上的成员照常可用（说明 cast() 的地址配对正确）
    CHECK(wrapper.get(String::newString("value")).asNumber().getInt32() == 7);
    CHECK(engine->evalScript(String::newString("getWidgetRef().getValue()")).asNumber().getInt32() == 7);

    // 其它策略走默认实现（兜底生效）-> PointerNativeInstance，接管所有权
    auto owned = engine->evalScript(String::newString("new Widget(11)")).asObject();
    auto ownedPayload = engine->getInstancePayload(owned);
    REQUIRE(ownedPayload != nullptr);
    CHECK(dynamic_cast<TrackingInstance*>(&ownedPayload->getHolder()) == nullptr);
    CHECK(ownedPayload->getHolder().is_owned() == true);
}

TEST_CASE_METHOD(FactoryTestFixture, "NativeInstanceFactory: prebuilt instance is passed through") {
    EngineScope scope{engine.get()};

    // 函数返回 std::unique_ptr<TrackingInstance>：直接作为承载实例，不再套一层
    auto wrapper = engine->evalScript(String::newString("makeWidget(42)")).asObject();
    auto payload = engine->getInstancePayload(wrapper);
    REQUIRE(payload != nullptr);

    auto* tracking = dynamic_cast<TrackingInstance*>(&payload->getHolder());
    REQUIRE(tracking != nullptr);
    CHECK(tracking->owned_ == true);

    // meta 来自实例本身（证明没有走类型表 / 没有套娃）
    CHECK(payload->getHolder().meta() == &WidgetMeta);

    // 成员访问照常
    CHECK(wrapper.get(String::newString("value")).asNumber().getInt32() == 42);

    // 返回 nullptr -> JS 侧是 null
    engine->globalThis().set(
        String::newString("makeNothing"),
        Function::newFunction(binding::cpp_func([]() -> std::unique_ptr<TrackingInstance> { return nullptr; }))
    );
    CHECK(engine->evalScript(String::newString("makeNothing() === null")).asBoolean().getValue());
}

TEST_CASE_METHOD(FactoryTestFixture, "NativeInstanceFactory: raw pointer transfers ownership") {
    EngineScope scope{engine.get()};

    // 裸指针沿用 jspp 对指针的一贯规则: 视为所有权转移
    auto* raw   = new TrackingInstance(&WidgetMeta, new Widget{1}, true); // 实例持有堆上的 Widget
    auto  taken = binding::detail::takeNativeInstance<TrackingInstance>(raw);
    REQUIRE(taken.get() == raw);

    // 空指针留给 JS 侧 null
    CHECK(binding::detail::takeNativeInstance<TrackingInstance>(static_cast<TrackingInstance*>(nullptr)) == nullptr);
}

} // namespace
