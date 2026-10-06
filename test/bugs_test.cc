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
    REQUIRE_THROWS(engine->evalScript(String::newString("\"use strict\"; new Derived(1).bar = 42")));
}


// ============================================================
// 瞬态作用域回归（TransientObjectScope：保活 + 溯源式跟踪）
// ============================================================

class Child {
    int id_{0};

public:
    explicit Child(int id) : id_{id} {}
    int getId() const { return id_; }
};
static auto ChildMeta = binding::defClass<Child>("Child").ctor<int>().method("getId", &Child::getId).build();

class EventWithChild {
    Child child_{7};

public:
    Child& getChild() { return child_; }

    static void listen(std::function<void(EventWithChild&)> cb) {
        EventWithChild ev;
        cb(ev);
    }
};
static auto EventWithChildMeta = binding::defClass<EventWithChild>("EventWithChild")
                                     .ctor(nullptr)
                                     // kReference 策略：回调内访问子属性会进入瞬态作用域托管
                                     .prop("child", &EventWithChild::getChild, nullptr, binding::ReturnValuePolicy::kReference)
                                     .func("listen", &EventWithChild::listen)
                                     .build();

// 场景 A：回调不持有事件参数。事件参数 wrapper 由 argv 持有引用计数，
// 作用域退出时 argv 先于 TransientObjectScope 析构 -> wrapper 被释放 (QuickJS 引用计数归零立即回收)
// -> InstancePayload/NativeInstance 被 finalizer 删除 -> TransientObjectScope 析构时
// 对已释放的 NativeInstance 调用 invalidate() -> use-after-free
TEST_CASE_METHOD(BugTestFixture, "Bug: transient scope UAF when callback does not retain event arg", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(EventWithChildMeta);
    engine->registerClass(ChildMeta);

    // 回调体内完全不持有 e，也不访问任何子属性
    REQUIRE_NOTHROW(
        engine->evalScript(String::newString(R"(
            EventWithChild.listen(() => { /* not retaining e */ });
        )"))
    );
}

// 场景 B：回调内访问子属性 (kReference) 并立即丢弃。
// wrapper 引用计数归零 -> 立即释放 -> NativeInstance 销毁，但仍在 trackedInstances_ 中
TEST_CASE_METHOD(BugTestFixture, "Bug: transient scope UAF when sub property dropped inside callback", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(EventWithChildMeta);
    engine->registerClass(ChildMeta);

    // x 创建后立即被丢弃，未逃逸；作用域退出时 invalidate 命中的是已释放内存
    REQUIRE_NOTHROW(
        engine->evalScript(String::newString(R"(
            EventWithChild.listen((e) => {
                let x = e.child; // kReference wrapper, 被瞬态作用域跟踪
                x = null;        // 立即丢弃 -> 引用计数归零 -> NativeInstance 被销毁
            });
        )"))
    );
}

// 场景 C：长期对象的成员在回调内被访问，不应被瞬态作用域误伤（溯源式跟踪）
class Holder {
    Child child_;

public:
    explicit Holder(int id) : child_(id) {}
    Child& getChild() { return child_; }
};
static auto HolderMeta = binding::defClass<Holder>("Holder")
                             .ctor<int>()
                             .prop("child", &Holder::getChild, nullptr, binding::ReturnValuePolicy::kReference)
                             .build();

TEST_CASE_METHOD(BugTestFixture, "Bug: transient scope must not poison long-lived member accessed in callback", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(EventWithChildMeta);
    engine->registerClass(ChildMeta);
    engine->registerClass(HolderMeta);

    // h 在瞬态作用域外创建（未被跟踪）；回调内访问 h.child（kReference，
    // parent=h 不在跟踪集合中）按溯源规则不进入托管，回调结束后引用必须仍然有效
    REQUIRE_NOTHROW(
        engine->evalScript(String::newString(R"(
            let h = new Holder(9);
            let captured;
            EventWithChild.listen(() => { captured = h.child; });
            captured.getId() === 9;
        )"))
    );
}

// 阳性对照：事件对象的子属性从瞬态根派生，逃逸后仍必须失效（原有语义保持）
TEST_CASE_METHOD(BugTestFixture, "Bug: transient scope event child must stay poisoned after escape", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(EventWithChildMeta);
    engine->registerClass(ChildMeta);

    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString(R"(
            let escaped;
            EventWithChild.listen((e) => { escaped = e.child; });
            // 此时 TransientObjectScope 已析构，escaped 应被标记为失效
            escaped.getId();
            throw new Error("Should not reach here");
        )")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Accessing destroyed instance of type"))
    );
}


// ============================================================
// 成员指针 prop 回归（默认引用语义 / const 解包修复）
// ============================================================

class PermsPod {
public:
    int water{0};
    int land{0};

    PermsPod() = default;
    explicit PermsPod(int w) : water(w) {}
};
static auto PermsPodMeta = binding::defClass<PermsPod>("PermsPod")
                               .ctor<int>()
                               .prop("water", &PermsPod::water)
                               .build();

class LandTablePod {
public:
    PermsPod environment{3};
    PermsPod role{5};
    PermsPod readonlyEnv{7};

    LandTablePod() = default;
};
static auto LandTablePodMeta = binding::defClass<LandTablePod>("LandTablePod")
                                   .ctor<>()
                                   // 成员指针 prop 默认自动升级为 kReferenceInternal：
                                   // 类类型成员按引用返回，写回生效、宿主保活
                                   .prop("environment", &LandTablePod::environment)
                                   .prop("role", &LandTablePod::role)
                                   // 显式 kCopy：仍为拷贝语义，不写回原成员
                                   .prop("env_copy", &LandTablePod::environment, binding::ReturnValuePolicy::kCopy)
                                   // prop_readonly：始终只读
                                   .prop_readonly("readonlyEnv", &LandTablePod::readonlyEnv)
                                   .build();

TEST_CASE_METHOD(BugTestFixture, "Bug: member pointer prop should reference nested POD member (write-back)", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(PermsPodMeta);
    engine->registerClass(LandTablePodMeta);

    // 修复前：kAutomatic 解析为 kCopy 且 const 解包 -> 抛 "Object is not copy constructible"
    auto result = engine->evalScript(String::newString(R"(
        let t = new LandTablePod();
        t.environment.water = 42; // 写回宿主成员
        t.role.water = 99;
        t.environment.water === 42 && t.role.water === 99;
    )"));
    REQUIRE(result.isBoolean());
    REQUIRE(result.asBoolean().getValue());
}

TEST_CASE_METHOD(BugTestFixture, "Bug: member pointer prop explicit kCopy must not affect original", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(PermsPodMeta);
    engine->registerClass(LandTablePodMeta);

    // 显式 kCopy：修改作用于拷贝，不影响原成员
    auto result = engine->evalScript(String::newString(R"(
        let t = new LandTablePod();
        t.env_copy.water = 1;
        t.environment.water === 3;
    )"));
    REQUIRE(result.isBoolean());
    REQUIRE(result.asBoolean().getValue());
}

// const 宿主（通过 const 引用暴露）-> 子成员包装必须只读
class ConstHolderPod {
public:
    LandTablePod pod_;

    ConstHolderPod() = default;
    LandTablePod const& getPod() const { return pod_; }
};
static auto ConstHolderPodMeta = binding::defClass<ConstHolderPod>("ConstHolderPod")
                                     .ctor<>()
                                     .prop("pod", &ConstHolderPod::getPod, nullptr, binding::ReturnValuePolicy::kReference)
                                     .build();

TEST_CASE_METHOD(BugTestFixture, "Bug: const host must yield read-only member wrapper", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(PermsPodMeta);
    engine->registerClass(LandTablePodMeta);
    engine->registerClass(ConstHolderPodMeta);

    // 宿主 const -> 成员引用只读，写入必须被拒绝
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString(R"(
            let h = new ConstHolderPod();
            h.pod.environment.water = 1;
        )")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Cannot unwrap const instance"))
    );
}

TEST_CASE_METHOD(BugTestFixture, "Bug: prop_readonly member pointer must stay read-only", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(PermsPodMeta);
    engine->registerClass(LandTablePodMeta);

    // prop_readonly 强制只读：即使宿主可变，成员引用也必须只读
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString(R"(
            let t = new LandTablePod();
            t.readonlyEnv.water = 1;
        )")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Cannot unwrap const instance"))
    );
}


// ============================================================
// const& 返回 + kCopy / lambda method 绑定 回归
// ============================================================

// 业务场景：POD 对象以 const& 返回，绑定层应拷贝出可变副本
class LandPerm {
public:
    int water{0};
    int land{0};

    LandPerm() = default;
    explicit LandPerm(int w) : water(w) {}
};
static auto LandPermMeta = binding::defClass<LandPerm>("LandPerm")
                               .ctor<int>()
                               .prop("water", &LandPerm::water)
                               .build();

class Land {
public:
    LandPerm table_{11};

    Land() = default;
    explicit Land(int w) : table_(w) {}
    LandPerm const& getPermTable() const { return table_; }
};
static auto LandMeta = binding::defClass<Land>("Land")
                           .ctor<int>()
                           // 成员函数指针 + 显式 kCopy：const& 返回 -> 可变副本（修复：kCopy 剥离 const）
                           .method("getTableCopy", &Land::getPermTable, binding::ReturnValuePolicy::kCopy)
                           // lambda 绑定 + kCopy：const& 返回 -> 可变副本（修复：method 支持 lambda）
                           .method(
                               "getTableLambda",
                               [](Land& self) -> LandPerm const& { return self.getPermTable(); },
                               binding::ReturnValuePolicy::kCopy
                           )
                           // lambda 绑定 + 默认策略：按值返回 -> kAutomatic 解析为 kCopy（修复：按值默认拷贝）
                           .method("getTableValue", [](Land& self) -> LandPerm {
                               return LandPerm{self.getPermTable().water + 1};
                           })
                           // lambda 绑定 + 参数（void 分支）
                           .method("setWater", [](Land& self, int w) { self.table_.water = w; })
                           // lambda builder 特判：返回 C& 时返回 thiz
                           .method("self", [](Land& self) -> Land& { return self; })
                           .build();

TEST_CASE_METHOD(BugTestFixture, "Bug: const-ref + kCopy must produce mutable copy", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(LandPermMeta);
    engine->registerClass(LandMeta);

    // 修复前：const LandPerm& + kCopy -> ElementType 带 const -> 抛 "Object is not copy constructible"
    auto result = engine->evalScript(String::newString(R"(
        let l = new Land(5);
        let t = l.getTableCopy();
        t.water = 42;                 // 副本必须可写
        l.getTableCopy().water === 5; // 原对象不变（副本独立）
    )"));
    REQUIRE(result.isBoolean());
    REQUIRE(result.asBoolean().getValue());
}

TEST_CASE_METHOD(BugTestFixture, "Bug: lambda method binding (const-ref kCopy / by-value automatic / args)", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(LandPermMeta);
    engine->registerClass(LandMeta);

    // 修复前：.method 模板按成员函数指针展开（(inst->*f)），lambda 无法编译
    // 各段脚本用 IIFE 包裹，避免共享全局作用域导致 let 重复声明
    // lambda + const& 返回 + kCopy -> 可变副本
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        (() => {
            let l = new Land(7);
            let t = l.getTableLambda();
            t.water = 42;
            return t.water === 42;
        })()
    )")));
    // lambda + 按值返回 + 默认策略 -> kAutomatic 解析为 kCopy
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        (() => {
            let l = new Land(7);
            let v = l.getTableValue();
            return v.water === 8;
        })()
    )")));
    // lambda + 参数（void 分支，写回宿主）
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        (() => {
            let l = new Land(7);
            l.setWater(9);
            return l.getTableCopy().water === 9;
        })()
    )")));
    // builder 特判：lambda 返回 C& -> thiz
    REQUIRE_NOTHROW(engine->evalScript(String::newString(R"(
        (() => {
            let l = new Land(7);
            return l.self() === l;
        })()
    )")));
}



// ============================================================
// ReferenceInternal 生命周期安全性（4 种释放场景）
//
// 父对象 Actor 有两种所有权来源：
//   - JS 持有：new Actor() 创建，包装器独占持有 C++ 对象（ValueNativeInstance 内联）
//   - C++ 持有：ActorWorld.acquire() 返回裸引用，包装器为非拥有型（kReference）
// 子包装器（a.state, kReferenceInternal）仅持有指向父对象内存的裸指针
// （Actor::ActorState&），并通过隐藏属性钉住父“包装器”不被回收。
//
// 安全性契约（每个场景都要验证父与子）：
//   1. 子可达期间，父的 C++ 内存绝不可被回收（否则子访问即 UAF）；
//   2. 父被清理后，子必须“联动失效”——访问抛出受控异常
//      ("Accessing destroyed instance")，而不是读到悬垂内存；
//   3. 释放路径不产生 double-free / 漏析构（析构计数恰好为 1）。
// ============================================================

static int g_actorDeleted = 0; // 父对象析构计数
static int g_stateDeleted = 0; // 子对象（Actor 内联成员）析构计数

class Actor {
public:
    struct ActorState {
        bool alive_{true};
        bool dead_{false};

        ~ActorState() { ++g_stateDeleted; }
    };

    int id{0};
    ActorState state_;

    Actor(int id) : id(id) {}
    ~Actor() { ++g_actorDeleted; }
};

// C++ 侧持有 Actor（unique_ptr 独占所有权），向 JS 暴露非拥有引用
// 注意：子对象只能经宿主派生、禁止脚本构造，但必须声明为实例类（.ctor(nullptr)），
// 否则 registerClass 会将其当作静态类处理
class ActorWorld {
    static inline std::unique_ptr<Actor> actor_;

public:
    static Actor& acquire(int id) {
        actor_ = std::make_unique<Actor>(id);
        return *actor_;
    }
    static Actor& current() { return *actor_; } // 复用既有对象，不触发析构
    static void release() { actor_.reset(); }   // C++ 主动 delete 父对象
    static bool held() { return actor_ != nullptr; }
};

static auto ActorStateMeta = binding::defClass<Actor::ActorState>("ActorState")
            .ctor(nullptr)
            .prop("alive", &Actor::ActorState::alive_)
            .prop("dead", &Actor::ActorState::dead_)
            .build();
static auto ActorMeta = binding::defClass<Actor>("Actor")
            .ctor<int>()
            .prop("id", &Actor::id)
            .prop("state", &Actor::state_, binding::ReturnValuePolicy::kReferenceInternal)
            .build();
static auto ActorWorldMeta = binding::defClass<void>("ActorWorld")
            .func("acquire", &ActorWorld::acquire, binding::ReturnValuePolicy::kReference)
            .func("current", &ActorWorld::current, binding::ReturnValuePolicy::kReference)
            .func("release", &ActorWorld::release)
            .func("held", &ActorWorld::held)
            .build();

// 场景 1：脚本持有对象（JS new）- 脚本主动释放
// 脚本 null 父引用 -> 子仍钉住父；子 null 后 -> 父子恰好各析构一次
TEST_CASE_METHOD(BugTestFixture, "ReferenceInternal: JS-held object - script releases refs", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(ActorMeta);
    engine->registerClass(ActorStateMeta);

    const int actorBase = g_actorDeleted;
    const int stateBase = g_stateDeleted;

    // 父对象状态正常：id 写回生效
    auto r1 = engine->evalScript(String::newString(R"(
        globalThis.a = new Actor(7);
        globalThis.s = a.state;
        a.id === 7 && s.alive === true && s.dead === false;
    )"));
    REQUIRE(r1.isBoolean());
    REQUIRE(r1.asBoolean().getValue());

    // 脚本释放父引用：子可达 -> 父内存绝不可被回收
    engine->evalScript(String::newString("globalThis.a = null;"));
    engine->gc();
    CHECK(g_actorDeleted == actorBase);
    // 子对象状态正常且仍可写回（证明父内存确实存活）
    auto r2 = engine->evalScript(String::newString("s.alive === true && (s.dead = true, s.dead === true);"));
    REQUIRE(r2.asBoolean().getValue());

    // 脚本释放子引用：隐藏属性解除 -> 父安全回收，恰好一次
    engine->evalScript(String::newString("globalThis.s = null;"));
    engine->gc();
#ifdef JSPP_BACKEND_QUICKJS
    // V8 的 gc() 只发出内存压力提示，不保证同步回收包装器，析构计数无法在这里校验
    CHECK(g_actorDeleted == actorBase + 1);
    CHECK(g_stateDeleted == stateBase + 1);
#endif
}

// 场景 2（父对象部分）：C++ 持有对象 - C++ 主动 delete
// C++ 删除父对象前，先对父包装器执行 invalidate（C++ 侧能做到的最佳配合），
// 验证父对象访问联动失效：必须抛受控异常，不得 UAF。
TEST_CASE_METHOD(BugTestFixture, "ReferenceInternal: C++-held object - C++ delete - parent linked invalidation", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(ActorMeta);
    engine->registerClass(ActorStateMeta);
    engine->registerClass(ActorWorldMeta);

    const int actorBase = g_actorDeleted;

    auto r = engine->evalScript(String::newString(R"(
        globalThis.a = ActorWorld.acquire(1);
        globalThis.s = a.state;
        a.id === 1 && s.alive === true; // 删除前父子状态正常
    )"));
    REQUIRE(r.asBoolean().getValue());

    // 模拟“尽责的 C++ 宿主”：delete 前使父包装器过期
    auto aVal = engine->globalThis().get(String::newString("a"));
    REQUIRE(aVal.isObject());
    engine->getInstancePayload(aVal.asObject())->getHolder().invalidate();
    ActorWorld::release(); // C++ delete
    CHECK(g_actorDeleted == actorBase + 1);

    // 父对象访问：已 invalidate -> 受控异常
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString("a.id")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Accessing destroyed instance"))
    );
}

// 场景 2（子对象部分）：C++ 持有对象 - C++ 主动 delete
// 即使父包装器已 invalidate，子包装器（kReferenceInternal，裸 ActorState*）
// 与父之间只有“JS 包装器级”的隐藏引用，无任何过期联动 -> 按契约应抛受控异常；
// 若机制缺失，此处将直接命中 ASan 的 heap-use-after-free。
TEST_CASE_METHOD(BugTestFixture, "ReferenceInternal: C++-held object - C++ delete - child linked invalidation", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(ActorMeta);
    engine->registerClass(ActorStateMeta);
    engine->registerClass(ActorWorldMeta);

    const int actorBase = g_actorDeleted;

    engine->evalScript(String::newString(R"(
        globalThis.a = ActorWorld.acquire(1);
        globalThis.s = a.state;
    )"));

    // C++ 侧持有父包装器句柄，脚本随后放弃引用（父包装器只由子的隐藏引用保活）
    Global<Object> aWrapper;
    aWrapper.reset(engine->globalThis().get(String::newString("a")).asObject());
    engine->evalScript(String::newString("globalThis.a = null;"));

    // C++ 持有对象时的做法：delete 前先 invalidate() 父包装器
    engine->getInstancePayload(aWrapper.get())->getHolder().invalidate();
    ActorWorld::release(); // C++ delete 父对象
    CHECK(g_actorDeleted == actorBase + 1);

    // 子对象访问：契约要求联动失效（受控异常），而非 UAF
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString("s.alive")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("Accessing destroyed instance"))
    );
}

// 场景 3：脚本持有对象（JS new）- C++ 主动清理
// C++ 侧 remove 掉 globalThis 上的脚本引用并 GC：父子随隐藏引用链一并回收，
// finalizer 顺序不得产生 UAF / double-free
TEST_CASE_METHOD(BugTestFixture, "ReferenceInternal: JS-held object - C++ cleans up", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(ActorMeta);
    engine->registerClass(ActorStateMeta);

    const int actorBase = g_actorDeleted;
    const int stateBase = g_stateDeleted;

    engine->evalScript(String::newString(R"(
        globalThis.a = new Actor(11);
        globalThis.s = a.state;
    )"));
    CHECK(g_actorDeleted == actorBase);

    // C++ 主动清理父引用：子仍钉住父 -> 不得析构
    auto gt = engine->globalThis();
    gt.remove(String::newString("a"));
    engine->gc();
    CHECK(g_actorDeleted == actorBase);

    // C++ 主动清理子引用：父子一并安全回收，各恰好一次
    gt.remove(String::newString("s"));
    engine->gc();
#ifdef JSPP_BACKEND_QUICKJS
    // V8 的 gc() 只发出内存压力提示，不保证同步回收包装器，析构计数无法在这里校验
    CHECK(g_actorDeleted == actorBase + 1);
    CHECK(g_stateDeleted == stateBase + 1);
#endif
}

// 场景 4：C++ 持有对象 - 脚本释放引用
// 非拥有型包装器被脚本丢弃 -> 只释放包装器，绝不销毁 C++ 对象；
// C++ 稍后 delete -> 恰好一次，无 double-free、无泄漏
TEST_CASE_METHOD(BugTestFixture, "ReferenceInternal: C++-held object - script releases refs", "[bugs]") {
    EngineScope lock{*engine};
    engine->registerClass(ActorMeta);
    engine->registerClass(ActorStateMeta);
    engine->registerClass(ActorWorldMeta);

    const int actorBase = g_actorDeleted;
    const int stateBase = g_stateDeleted;

    // 脚本释放 a 的引用、保留 s：父包装器被子钉住，状态正常
    auto r1 = engine->evalScript(String::newString(R"(
        globalThis.a = ActorWorld.acquire(9);
        globalThis.s = a.state;
        globalThis.a = null;
        s.alive === true;
    )"));
    REQUIRE(r1.asBoolean().getValue());
    CHECK(ActorWorld::held());

    // 脚本释放 s：两个非拥有包装器先后回收，C++ 对象必须毫发无损
    engine->evalScript(String::newString("globalThis.s = null;"));
    engine->gc();
    CHECK(g_actorDeleted == actorBase);
    CHECK(ActorWorld::held());

    // 对象状态正常：把同一个 C++ 对象重新暴露给脚本，读写完好（未被包装器回收误伤）
    auto r2 = engine->evalScript(String::newString(R"(
        globalThis.b = ActorWorld.current();
        b.id === 9 && b.state.alive === true;
    )"));
    REQUIRE(r2.asBoolean().getValue());
    engine->evalScript(String::newString("globalThis.b = null;"));
    engine->gc();

    // C++ 最终 delete：恰好析构一次（前面 GC 未产生 double-free）
    ActorWorld::release();
    CHECK(g_actorDeleted == actorBase + 1);
    CHECK(g_stateDeleted == stateBase + 1);
}


} // namespace
