#pragma once
#include "jspp/Macro.h"
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Fwd.h"
#include "jspp/core/Reference.h"

#include "QjsHelper.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

JSPP_WARNING_GUARD_BEGIN
#include "quickjs.h"
JSPP_WARNING_GUARD_END

#include <string>

namespace jspp {

namespace qjs_backend {

void QjsExceptionContext::extractMessage() const noexcept try {
    if (!message.empty()) {
        return;
    }
    auto err = error.get();
    if (err.isObject()) {
        auto obj        = err.asObject();
        auto messageKey = String::newString("message");
        if (obj.has(messageKey)) {
            auto msg = obj.get(messageKey);
            message  = msg.toString().getValue(); // String.toString return self
            return;
        }
    }
    message = err.toString().getValue(); // fallback
} catch (...) {
    message = "[ERROR: Could not get exception message]";
}

} // namespace qjs_backend

Exception::Exception(Local<Value> const& exception) : std::exception(), ctx_(std::make_shared<ExceptionContext>()) {
    ctx_->error.reset(exception);
}

Exception::Exception(Local<String> const& message, Type type)
: std::exception(),
  ctx_(std::make_shared<ExceptionContext>()) {
    ctx_->type    = type;
    ctx_->message = message.getValue();
    makeException(); // null exception, make it
}

Exception::Exception(std::string message, Type type) : std::exception(), ctx_(std::make_shared<ExceptionContext>()) {
    ctx_->type    = type;
    ctx_->message = std::move(message);
    // C++ throw, lazy make exception
}


Exception::Type Exception::type() const noexcept { return ctx_->type; }

char const* Exception::what() const noexcept {
    ctx_->extractMessage();
    return ctx_->message.c_str();
}

std::string Exception::message() const noexcept {
    ctx_->extractMessage();
    return ctx_->message;
}

std::string Exception::stacktrace() const noexcept try {
    return ctx_->error.get().asObject().get(String::newString("stack")).toString().getValue();
} catch (...) {
    return "[ERROR: Could not get stacktrace]";
}

Local<Value> Exception::exception() const noexcept {
    if (ctx_->error.isEmpty()) {
        makeException();
    }
    return ctx_->error.getValue();
}

void Exception::makeException() const {
    auto ctx = qjs_backend::QjsHelper::currentContextChecked();
    switch (ctx_->type) {
    case ExceptionType::Unknown:
    case ExceptionType::Error:
        JS_ThrowPlainError(ctx, "%s", ctx_->message.c_str());
        break;
    case ExceptionType::RangeError:
        JS_ThrowRangeError(ctx, "%s", ctx_->message.c_str());
        break;
    case ExceptionType::ReferenceError:
        JS_ThrowReferenceError(ctx, "%s", ctx_->message.c_str());
        break;
    case ExceptionType::SyntaxError:
        JS_ThrowSyntaxError(ctx, "%s", ctx_->message.c_str());
        break;
    case ExceptionType::TypeError:
        JS_ThrowTypeError(ctx, "%s", ctx_->message.c_str());
        break;
    }
    auto exc = ValueHelper::wrap<Value>(JS_GetException(ctx));
    ctx_->error.reset(exc);
}


} // namespace jspp