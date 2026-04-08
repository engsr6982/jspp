#pragma once
#include "Fwd.h"
#include "jspp/Macro.h"

#include <exception>
#include <memory>
#include <string>
#include <type_traits>


#include "jspp-backend/traits/TraitException.h"

namespace jspp {


class Exception final : public std::exception {
public:
    using Type = ExceptionType;

    explicit Exception(Local<Value> const& exception);
    explicit Exception(Local<String> const& message, Type type = Type::Error);
    explicit Exception(std::string message, Type type = Type::Error);

    // The C++ standard requires exception classes to be reproducible
    Exception(Exception const&)                = default;
    Exception& operator=(Exception const&)     = default;
    Exception(Exception&&) noexcept            = default;
    Exception& operator=(Exception&&) noexcept = default;

    [[nodiscard]] Type type() const noexcept;

    [[nodiscard]] char const* what() const noexcept override;

    [[nodiscard]] std::string message() const noexcept;

    [[nodiscard]] std::string stacktrace() const noexcept;

    [[nodiscard]] Local<Value> exception() const noexcept;

private:
    void makeException() const;

    /**
     * Global handle does not allow copying of resources,
     * but the C++ standard requires exception classes to be replicated
     */
    using ExceptionContext = internal::ImplType<Exception>::type;
    std::shared_ptr<ExceptionContext> ctx_{nullptr};
};


} // namespace jspp