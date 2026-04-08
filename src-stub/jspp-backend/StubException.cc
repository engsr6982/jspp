#pragma once
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Fwd.h"
#include "jspp/core/Reference.h"

#include "StubHelper.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

#include <string>

namespace jspp {

namespace stub_backend {

void StubExceptionContext::extractMessage() const noexcept try {
    if (!message.empty()) {
        return;
    }
    // TODO: please implement this
    message = "[ERROR: StubExceptionContext::extractMessage() not implemented]";
} catch (...) {
    message = "[ERROR: Could not get exception message]";
}

} // namespace stub_backend

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

std::string Exception::stacktrace() const noexcept {
    // TODO: please implement this
    return "[ERROR: Could not get stacktrace]";
}

Local<Value> Exception::exception() const noexcept {
    // TODO: please implement this
    // Before returning the Local handle, please check whether the handle is null
    // If it is empty, please call makeException before returning
}

void Exception::makeException() const {
    // TODO: please implement this
    // Please construct different script exceptions based on the type of exception
}


} // namespace jspp