#pragma once
#include "jspp/Macro.h"
#include "jspp/core/Fwd.h"
#include "jspp/core/Reference.h"
#include "jspp/core/Value.h"

#include <string_view>

namespace jspp {
struct ClassMeta;
}


namespace jspp::stub_backend {


class StubEngine {
public:
    JSPP_DISABLE_COPY(StubEngine);

protected:
    explicit StubEngine();

    ~StubEngine();

public:
    // stub backend specific

protected:
    [[nodiscard]] Engine*       asEngine();
    [[nodiscard]] Engine const* asEngine() const;

    void setToStringTag(Local<Object>& obj, std::string_view name, bool hasConstructor);
    void setToStringTag(Local<Object>& obj, std::string_view name);

    Local<Function> newConstructor(ClassMeta const& meta);

    void buildStaticMembers(Local<Object>& obj, ClassMeta const& meta);
    void buildInstanceMembers(Local<Object>& obj, ClassMeta const& meta);

    friend class StubEngineScope;
    friend class StubExitEngineScope;
    friend class StubStackFrameScope;
    friend struct StubHelper;

    // This symbol is used to mark the construction of objects from C++ (with special logic).
    Global<Symbol> constructorSymbol_{};

    // Mark C++ as a bound function to prevent an infinite loop when JS dispatches an override
    Global<Symbol> nativeFunctionTag_{};
};


} // namespace jspp::stub_backend