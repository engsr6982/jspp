#pragma once
#include "Fwd.h"
#include "jspp/Macro.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Value.h"

#include <memory>
#include <type_traits>
#include <variant>

#include "jspp-backend/traits/TraitReference.h"


namespace jspp {

/**
 * For performance optimization, use MaybeLocal instead of Local for operations that might fail;
 * throw can avoid the overhead of C++ exceptions. If needed, you can rethrow the C++ exception.
 */
template <typename T>
class MaybeLocal {
    static_assert(std::is_base_of_v<Value, T>, "T must be derived from Value");

    struct MessageError {
        std::string   message{};
        ExceptionType type{ExceptionType::Unknown};
    };
    // Put MessageError on the heap to avoid MaybeLocal from getting too big.
    using MessageErrorPtr = std::shared_ptr<MessageError const>;

    struct ExceptionValue {
        Local<Value> handle{};
    };
    static_assert(sizeof(ExceptionValue) == sizeof(Local<Value>));

    using LocalTypeImpl = internal::ImplType<Local<T>>::type;
    using Storage       = std::variant<LocalTypeImpl, MessageErrorPtr, ExceptionValue>;
    Storage impl;

    template <typename>
    friend class Local;

    friend ValueHelper;

    template <typename>
    friend class MaybeLocal; // self forward

    /**
     * Because different engine backends have different handle capabilities,
     *  for cross-engine safety, the explicit success path must have its status
     *  set by the Local<T> handle.
     */
    explicit MaybeLocal(LocalTypeImpl raw) : impl(std::move(raw)) {} // for Local<T>
    explicit MaybeLocal(ExceptionValue err) : impl(std::move(err)) {}
    explicit MaybeLocal(MessageErrorPtr err) : impl(std::move(err)) {}

    static_assert(std::is_nothrow_move_constructible_v<Storage>);
    static_assert(std::is_nothrow_move_assignable_v<Storage>);

public:
    JSPP_DISABLE_NEW();

    // impl by specific backend
    MaybeLocal(); // undefined
    MaybeLocal(Local<T> const& success);
    ~MaybeLocal();
    MaybeLocal(MaybeLocal const& cp);
    MaybeLocal& operator=(MaybeLocal const& cp);
    MaybeLocal(MaybeLocal&& mv) noexcept;
    MaybeLocal& operator=(MaybeLocal&& mv) noexcept;

    [[nodiscard]] static MaybeLocal<T> fromException(Local<Value> err) {
        return MaybeLocal<T>{ExceptionValue{std::move(err)}};
    }
    [[nodiscard]] static MaybeLocal<T> fromError(std::string message, ExceptionType type = ExceptionType::Error) {
        return MaybeLocal<T>{std::make_shared<MessageError>(std::move(message), type)};
    }
    template <typename S>
    [[nodiscard]] static MaybeLocal<T> forward(MaybeLocal<S> const& mv) {
        MaybeLocal<T> ret;
        ret = std::move(mv);
        return ret;
    }

    [[nodiscard]] bool hasValue() const noexcept { return std::holds_alternative<LocalTypeImpl>(impl); }
    [[nodiscard]] bool hasException() const noexcept { return !hasValue(); }

    [[nodiscard]] Local<T> toLocal() const; // engine-specific

    [[nodiscard]] Exception toException() const {
        if (hasValue()) {
            throw std::runtime_error{"MaybeLocal::toException() called on non-exceptional MaybeLocal"};
        }
        if (auto* e = std::get_if<ExceptionValue>(&impl)) {
            return Exception{e->handle};
        } else if (auto* e = std::get_if<MessageErrorPtr>(&impl)) {
            return Exception{(*e)->message, (*e)->type};
        } else {
            [[unlikely]] throw std::runtime_error{"MaybeLocal::toException() called on invalid MaybeLocal"};
        }
    }

    operator bool() const { return hasValue(); }

    // TODO: Implement
    // template <typename F>
    // auto transform(F&& f) const {}

    // template <typename F>
    // auto andThen(F&& f) const {}

    // template <typename F>
    // auto orElse(F&& f) const {}
};

} // namespace jspp

#include "jspp-backend/MaybeLocal.inl"
