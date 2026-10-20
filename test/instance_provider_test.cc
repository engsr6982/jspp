#include "catch2/catch_test_macros.hpp"
#include "catch2/matchers/catch_matchers_exception.hpp"
#include "catch2/matchers/catch_matchers_string.hpp"

#include "jspp/Jspp.h"
#include "jspp/binding/MetaBuilder.h"
#include "jspp/binding/ReturnValuePolicy.h"
#include "jspp/binding/TypeConverter.h"
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Trampoline.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace {

using namespace jspp;
using namespace jspp::binding;

// =============================================================================
// Scenario: C++ owns a singleton/manager while the script side should only see
// a set of static functions:
//   defClass<void>(...).instanceProvider(...).func("x", &T::method).build();
// jspp obtains the instance from the provider on every script call and then
// invokes the member function. The script side receives a plain object; no
// instance wrapper is created and no InstancePayload needs to be unwrapped.
// =============================================================================

class DataRegistry {
    DataRegistry() = default;

public:
    struct DataEntry {
        std::string name;
        std::string uniqueId;
        std::string desc;
        std::string payload;

        DataEntry(std::string name_, std::string uniqueId_, std::string desc_, std::string payload_)
        : name(std::move(name_)),
          uniqueId(std::move(uniqueId_)),
          desc(std::move(desc_)),
          payload(std::move(payload_)) {}
    };

    using DataEntryOpt = std::optional<DataEntry>;

    static DataRegistry& instance() {
        static DataRegistry registry;
        return registry;
    }

    DataEntryOpt fromName(std::string_view name) const {
        return DataEntry{std::string{name}, "1234567890", "test", "test"};
    }

    DataEntryOpt fromId(std::string_view id) const {
        return DataEntry{"test", std::string{id}, "test", "test"};
    }

    DataEntryOpt notFound() const { return std::nullopt; }

    DataEntry const& cached() const {
        static DataEntry const entry{"cached", "cached-id", "cached-desc", "cached-payload"};
        return entry;
    }

    int constMethod() const { return 1; }
    int mutableMethod() { return ++mutableCalls_; }

    std::string describe(std::string_view name) const { return "name:" + std::string{name}; }
    std::string describe(std::string_view name, std::string_view suffix) const {
        return "name:" + std::string{name} + std::string{suffix};
    }

private:
    int mutableCalls_ = 0;
};

// Provider call counter (verifies the provider runs on every script call).
int           g_providerCalls = 0;
DataRegistry& countingProvider() {
    ++g_providerCalls;
    return DataRegistry::instance();
}

DataRegistry*       nullProvider() { return nullptr; }
DataRegistry*       pointerProvider() { return &DataRegistry::instance(); }
DataRegistry const& constRefProvider() { return DataRegistry::instance(); }
DataRegistry const* constPointerProvider() { return &DataRegistry::instance(); }

// The provider returns a derived instance while member functions are declared on the base:
// a legal derived-to-base upcast must be accepted.
struct RegistryBase {
    int mutableValue() { return ++value_; }
    int constValue() const { return value_; }

    int value_ = 0;
};
struct RegistryDerived : RegistryBase {};

// Every .func() captures its own copy of the provider, so state that must be shared across methods
// lives outside the provider.
RegistryDerived g_derivedRegistry;

struct DerivedProvider {
    RegistryDerived& operator()() { return g_derivedRegistry; }
};
struct ConstDerivedProvider {
    RegistryDerived const& operator()() { return g_derivedRegistry; }
};

// -----------------------------------------------------------------------------
// Compile-time constraint checks
// -----------------------------------------------------------------------------
using RefProvider      = decltype([]() -> DataRegistry& { return DataRegistry::instance(); });
using ConstRefProvider = decltype([]() -> DataRegistry const& { return DataRegistry::instance(); });
using PtrProvider      = decltype([]() -> DataRegistry* { return &DataRegistry::instance(); });
using ConstPtrProvider = decltype([]() -> DataRegistry const* { return &DataRegistry::instance(); });
using ValueProvider    = decltype([]() { return DataRegistry::instance(); });

// Provider classification.
static_assert(traits::InstanceProvider<RefProvider>);
static_assert(traits::ReferenceInstanceProvider<RefProvider>);
static_assert(!traits::PointerInstanceProvider<RefProvider>);
static_assert(traits::BorrowingInstanceProvider<RefProvider>);

static_assert(traits::ReferenceInstanceProvider<ConstRefProvider>);
static_assert(traits::PointerInstanceProvider<PtrProvider>);
static_assert(traits::PointerInstanceProvider<ConstPtrProvider>);

// A value-returning provider is callable but does not borrow its result.
static_assert(traits::InstanceProvider<ValueProvider>);
static_assert(!traits::BorrowingInstanceProvider<ValueProvider>);

// Normalized return form.
static_assert(!traits::InstanceProviderTraits<RefProvider>::is_const);
static_assert(std::is_same_v<traits::InstanceProviderTraits<RefProvider>::Ref, DataRegistry&>);

static_assert(traits::InstanceProviderTraits<ConstRefProvider>::is_const);
static_assert(std::is_same_v<traits::InstanceProviderTraits<ConstRefProvider>::Ref, DataRegistry const&>);

static_assert(std::is_same_v<traits::InstanceProviderTraits<PtrProvider>::Element, DataRegistry>);
static_assert(std::is_same_v<traits::InstanceProviderTraits<PtrProvider>::Ref, DataRegistry&>);
static_assert(traits::InstanceProviderTraits<ConstPtrProvider>::is_const);
static_assert(std::is_same_v<traits::InstanceProviderTraits<ConstPtrProvider>::Ref, DataRegistry const&>);

// Compatibility between the provider's instance type and the member function's class.
using ConstMethod = decltype(&DataRegistry::constMethod);
using MutMethod   = decltype(&DataRegistry::mutableMethod);

static_assert(traits::InstanceProviderFor<RefProvider, ConstMethod>);
static_assert(traits::InstanceProviderFor<RefProvider, MutMethod>);
static_assert(traits::InstanceProviderFor<ConstRefProvider, ConstMethod>);
// A const instance cannot be bound to a non-const member function.
static_assert(!traits::InstanceProviderFor<ConstRefProvider, MutMethod>);

static_assert(traits::InstanceProviderFor<PtrProvider, MutMethod>);
static_assert(traits::InstanceProviderFor<ConstPtrProvider, ConstMethod>);
static_assert(!traits::InstanceProviderFor<ConstPtrProvider, MutMethod>);
static_assert(!traits::InstanceProviderFor<ValueProvider, ConstMethod>);
// Free/static function pointers are not instance methods.
static_assert(!traits::InstanceProviderFor<RefProvider, decltype(&DataRegistry::instance)>);

// A derived provider can be bound to base member functions.
static_assert(traits::InstanceProviderFor<DerivedProvider, decltype(&RegistryBase::mutableValue)>);
static_assert(traits::InstanceProviderFor<ConstDerivedProvider, decltype(&RegistryBase::constValue)>);
static_assert(!traits::InstanceProviderFor<ConstDerivedProvider, decltype(&RegistryBase::mutableValue)>);

// Trampoline subclasses are rejected as provider return types (checked by the static_assert in
// MetaBuilder::instanceProvider).
class TrampolineRegistry : public enable_trampoline {
public:
    int value() const { return 1; }
};
static_assert(std::is_base_of_v<enable_trampoline, TrampolineRegistry>);

// -----------------------------------------------------------------------------
// Metadata
// -----------------------------------------------------------------------------
auto const DataEntryMeta = defClass<DataRegistry::DataEntry>("DataEntry")
                               .ctor<std::string, std::string, std::string, std::string>()
                               .prop("name", &DataRegistry::DataEntry::name)
                               .prop("uniqueId", &DataRegistry::DataEntry::uniqueId)
                               .prop("desc", &DataRegistry::DataEntry::desc)
                               .prop("payload", &DataRegistry::DataEntry::payload)
                               .build();

auto const DataRegistryMeta = defClass<void>("DataRegistry")
                                  .instanceProvider([]() -> DataRegistry& { return DataRegistry::instance(); })
                                  .func("fromName", &DataRegistry::fromName)
                                  .func("fromId", &DataRegistry::fromId)
                                  .func("notFound", &DataRegistry::notFound)
                                  .func(
                                      "describe",
                                      static_cast<std::string (DataRegistry::*)(std::string_view) const>(
                                          &DataRegistry::describe
                                      ),
                                      static_cast<std::string (DataRegistry::*)(std::string_view, std::string_view)
                                                       const>(&DataRegistry::describe)
                                  )
                                  .build();

auto const CountingRegistryMeta = defClass<void>("CountingRegistry")
                                      .instanceProvider(&countingProvider)
                                      .func("fromId", &DataRegistry::fromId)
                                      .build();

auto const NullRegistryMeta = defClass<void>("NullRegistry")
                                  .instanceProvider(&nullProvider)
                                  .func("fromName", &DataRegistry::fromName)
                                  .build();

auto const PointerRegistryMeta = defClass<void>("PointerRegistry")
                                     .instanceProvider(&pointerProvider)
                                     .func("fromName", &DataRegistry::fromName)
                                     .build();

auto const ConstRegistryMeta = defClass<void>("ConstRegistry")
                                   .instanceProvider(&constRefProvider)
                                   .func("fromName", &DataRegistry::fromName)
                                   .func("cached", &DataRegistry::cached, ReturnValuePolicy::kReferencePersistent)
                                   .build();

auto const ConstPointerRegistryMeta = defClass<void>("ConstPointerRegistry")
                                          .instanceProvider(&constPointerProvider)
                                          .func("fromId", &DataRegistry::fromId)
                                          .build();

auto const DerivedRegistryMeta = defClass<void>("DerivedRegistry")
                                     .instanceProvider(DerivedProvider{})
                                     .func("mutableValue", &RegistryBase::mutableValue)
                                     .func("constValue", &RegistryBase::constValue)
                                     .build();

// Ordinary static bindings used alongside provider-backed methods.
int mixedAdd(int a, int b) { return a + b; }
std::string mixedConcat(std::string a, std::string b) { return a + b; }
std::string mixedConcat(std::string a, int b) { return a + std::to_string(b); }

// Provider-backed methods mixed with ordinary static functions, overloads, and a property.
auto const MixedRegistryMeta = defClass<void>("MixedRegistry")
                                   .instanceProvider(&pointerProvider)
                                   .func("fromName", &DataRegistry::fromName)
                                   .func("add", &mixedAdd)
                                   .func(
                                       "concat",
                                       static_cast<std::string (*)(std::string, std::string)>(&mixedConcat),
                                       static_cast<std::string (*)(std::string, int)>(&mixedConcat)
                                   )
                                   .var_readonly("version", std::string{"1.0"})
                                   .build();

// Ordinary static functions may also be registered before the provider is configured.
auto const LateProviderMeta = defClass<void>("LateProvider")
                                  .func("add", &mixedAdd)
                                  .instanceProvider(&pointerProvider)
                                  .func("fromId", &DataRegistry::fromId)
                                  .build();

// Regression: a static class without a provider keeps the original binding behavior.
auto const LegacyStaticMeta = defClass<void>("LegacyStatic")
                                  .func("add", &mixedAdd)
                                  .func(
                                      "concat",
                                      static_cast<std::string (*)(std::string, std::string)>(&mixedConcat),
                                      static_cast<std::string (*)(std::string, int)>(&mixedConcat)
                                  )
                                  .var_readonly("version", std::string{"1.0"})
                                  .build();

struct InstanceProviderFixture {
    std::unique_ptr<Engine> engine;

    InstanceProviderFixture() : engine(std::make_unique<Engine>()) {}
};

// -----------------------------------------------------------------------------
// Runtime tests
// -----------------------------------------------------------------------------
TEST_CASE_METHOD(InstanceProviderFixture, "InstanceProvider: binds instance methods as static functions") {
    EngineScope scope{engine.get()};

    engine->registerClass(DataEntryMeta);
    engine->registerClass(DataRegistryMeta);

    auto result = engine->evalScript(String::newString(
        "[DataRegistry.fromName('alpha').name, DataRegistry.fromName('alpha').uniqueId, "
        "DataRegistry.fromName('alpha').desc, DataRegistry.fromName('alpha').payload].join('|')"
    ));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "alpha|1234567890|test|test");

    result = engine->evalScript(String::newString("DataRegistry.fromId('id-42').uniqueId"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "id-42");

    // An empty std::optional<T> maps to JS null.
    result = engine->evalScript(String::newString("DataRegistry.notFound()"));
    REQUIRE(result.isNull());

    // Overload set of member function pointers.
    result = engine->evalScript(String::newString("DataRegistry.describe('alpha')"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "name:alpha");

    result = engine->evalScript(String::newString("DataRegistry.describe('alpha', '!')"));
    REQUIRE(result.isString());
    REQUIRE(result.asString().getValue() == "name:alpha!");

    // Argument count mismatch.
    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString("DataRegistry.fromName()")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("argument count mismatch"))
    );

    // Still a static class: instances cannot be constructed from script.
    REQUIRE_THROWS(engine->evalScript(String::newString("new DataRegistry()")));
}

// Kept in its own test case: the no-overload exception path is known to trip an exception-unwinding
// stack overflow under clang-cl + ASan (a toolchain artifact unrelated to this feature), so ASan runs
// can exclude it independently.
TEST_CASE_METHOD(InstanceProviderFixture, "InstanceProvider: overload dispatch reports no overload") {
    EngineScope scope{engine.get()};

    engine->registerClass(DataEntryMeta);
    engine->registerClass(DataRegistryMeta);

    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString("DataRegistry.describe(1)")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("no overload found"))
    );
}

TEST_CASE_METHOD(InstanceProviderFixture, "InstanceProvider: provider is invoked on every call") {
    EngineScope scope{engine.get()};

    engine->registerClass(DataEntryMeta);
    engine->registerClass(CountingRegistryMeta);

    g_providerCalls = 0;
    engine->evalScript(String::newString("CountingRegistry.fromId('a')"));
    engine->evalScript(String::newString("CountingRegistry.fromId('b')"));
    REQUIRE(g_providerCalls == 2);

    auto result = engine->evalScript(String::newString("CountingRegistry.fromId('c').uniqueId"));
    REQUIRE(result.asString().getValue() == "c");
    REQUIRE(g_providerCalls == 3);
}

TEST_CASE_METHOD(InstanceProviderFixture, "InstanceProvider: pointer and const provider variants") {
    EngineScope scope{engine.get()};

    engine->registerClass(DataEntryMeta);
    engine->registerClass(PointerRegistryMeta);
    engine->registerClass(ConstRegistryMeta);
    engine->registerClass(ConstPointerRegistryMeta);

    auto result = engine->evalScript(String::newString("PointerRegistry.fromName('ptr').name"));
    REQUIRE(result.asString().getValue() == "ptr");

    result = engine->evalScript(String::newString("ConstRegistry.fromName('const').name"));
    REQUIRE(result.asString().getValue() == "const");

    result = engine->evalScript(String::newString("ConstRegistry.cached().payload"));
    REQUIRE(result.asString().getValue() == "cached-payload");

    result = engine->evalScript(String::newString("ConstPointerRegistry.fromId('cptr').uniqueId"));
    REQUIRE(result.asString().getValue() == "cptr");
}

TEST_CASE_METHOD(InstanceProviderFixture, "InstanceProvider: ordinary static bindings are unaffected") {
    EngineScope scope{engine.get()};

    engine->registerClass(DataEntryMeta);
    engine->registerClass(MixedRegistryMeta);
    engine->registerClass(LateProviderMeta);
    engine->registerClass(LegacyStaticMeta);

    // Provider-backed member methods.
    auto result = engine->evalScript(String::newString("MixedRegistry.fromName('mix').name"));
    REQUIRE(result.asString().getValue() == "mix");

    // Ordinary static function on the same class.
    result = engine->evalScript(String::newString("MixedRegistry.add(2, 3)"));
    REQUIRE(result.asNumber().getInt32() == 5);

    // Ordinary static overload set on the same class.
    result = engine->evalScript(String::newString("MixedRegistry.concat('a', 'b')"));
    REQUIRE(result.asString().getValue() == "ab");

    result = engine->evalScript(String::newString("MixedRegistry.concat('a', 1)"));
    REQUIRE(result.asString().getValue() == "a1");

    // Static property on the same class.
    result = engine->evalScript(String::newString("MixedRegistry.version"));
    REQUIRE(result.asString().getValue() == "1.0");

    // Static functions registered before the provider are preserved across the builder transition.
    result = engine->evalScript(String::newString("LateProvider.add(4, 5)"));
    REQUIRE(result.asNumber().getInt32() == 9);

    result = engine->evalScript(String::newString("LateProvider.fromId('late').uniqueId"));
    REQUIRE(result.asString().getValue() == "late");

    // A static class without a provider behaves exactly as before.
    result = engine->evalScript(String::newString("LegacyStatic.add(6, 7)"));
    REQUIRE(result.asNumber().getInt32() == 13);

    result = engine->evalScript(String::newString("LegacyStatic.concat('x', 2)"));
    REQUIRE(result.asString().getValue() == "x2");

    result = engine->evalScript(String::newString("LegacyStatic.version"));
    REQUIRE(result.asString().getValue() == "1.0");
}

TEST_CASE_METHOD(InstanceProviderFixture, "InstanceProvider: null pointer is rejected at call time") {
    EngineScope scope{engine.get()};

    engine->registerClass(DataEntryMeta);
    engine->registerClass(NullRegistryMeta);

    REQUIRE_THROWS_MATCHES(
        engine->evalScript(String::newString("NullRegistry.fromName('x')")),
        Exception,
        Catch::Matchers::MessageMatches(Catch::Matchers::ContainsSubstring("null pointer"))
    );
}

TEST_CASE_METHOD(InstanceProviderFixture, "InstanceProvider: derived instance can be bound to base methods") {
    EngineScope scope{engine.get()};

    engine->registerClass(DerivedRegistryMeta);

    // Every call re-invokes its provider copy, all returning the same external instance, so state
    // is preserved across calls and methods.
    g_derivedRegistry.value_ = 0;
    auto result = engine->evalScript(String::newString("DerivedRegistry.mutableValue()"));
    REQUIRE(result.asNumber().getInt32() == 1);

    result = engine->evalScript(String::newString("DerivedRegistry.mutableValue()"));
    REQUIRE(result.asNumber().getInt32() == 2);

    result = engine->evalScript(String::newString("DerivedRegistry.constValue()"));
    REQUIRE(result.asNumber().getInt32() == 2);
}

TEST_CASE("InstanceProvider: provider outlives the builder") {
    // The builder is destroyed right after build(); each generated callback owns its provider copy.
    ClassMeta meta = defClass<void>("EphemeralRegistry")
                         .instanceProvider([]() -> DataRegistry& { return DataRegistry::instance(); })
                         .func("fromName", &DataRegistry::fromName)
                         .build();

    // Declaration order matters: meta must outlive the engine (the engine stores a ClassMeta pointer).
    auto engine = std::make_unique<Engine>();
    {
        EngineScope scope{engine.get()};
        engine->registerClass(DataEntryMeta);
        engine->registerClass(meta);

        auto result = engine->evalScript(String::newString("EphemeralRegistry.fromName('alive').name"));
        REQUIRE(result.asString().getValue() == "alive");
    }
    engine.reset();
}

} // namespace
