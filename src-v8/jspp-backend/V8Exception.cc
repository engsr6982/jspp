#pragma once
#include "jspp/core/EngineScope.h"
#include "jspp/core/Exception.h"
#include "jspp/core/Fwd.h"
#include "jspp/core/Reference.h"

#include "V8Helper.h"
#include "jspp/core/Value.h"
#include "jspp/core/ValueHelper.h"

#include <string>

JSPP_WARNING_GUARD_BEGIN
#include <v8-exception.h>
#include <v8-local-handle.h>
#include <v8-persistent-handle.h>
#include <v8-primitive.h>
#include <v8-value.h>
#include <v8.h>
JSPP_WARNING_GUARD_END


namespace jspp {

namespace v8_backend {

void V8ExceptionContext::extractMessage() const noexcept try {
    if (!message.empty()) {
        return;
    }
    auto isolate = V8Helper::currentIsolateChecked();
    auto vtry    = v8::TryCatch{isolate};

    auto msg = v8::Exception::CreateMessage(isolate, error.Get(isolate));
    if (!msg.IsEmpty()) {
        message = ValueHelper::wrap<String>(msg->Get()).getValue();
    }
} catch (...) {
    message = "[ERROR: Could not get exception message]";
}

} // namespace v8_backend

Exception::Exception(Local<Value> const& exception) : std::exception(), ctx_(std::make_shared<ExceptionContext>()) {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();

    ctx_->error = v8::Global<v8::Value>(isolate, ValueHelper::unwrap(exception));
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
    auto&& [isolate, ctx] = v8_backend::V8Helper::currentIsolateContextChecked();

    auto vtry = v8::TryCatch{isolate}; // noexcept

    auto exc   = ValueHelper::unwrap(exception());
    auto stack = v8::TryCatch::StackTrace(ctx, exc);
    if (!stack.IsEmpty()) {
        v8::String::Utf8Value ut8{isolate, stack.ToLocalChecked()};
        if (auto str = *ut8) {
            return str;
        }
    }
    return "[ERROR: Could not get stacktrace]";
}

Local<Value> Exception::exception() const noexcept {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();

    if (ctx_->error.IsEmpty()) {
        makeException();
    }

    auto v8local = ctx_->error.Get(isolate);
    return ValueHelper::wrap<Value>(v8local);
}

void Exception::makeException() const {
    auto isolate = v8_backend::V8Helper::currentIsolateChecked();

    v8::Local<v8::Value> exception;
    {
        switch (ctx_->type) {
        case Type::Unknown:
        case Type::Error:
            exception = v8::Exception::Error(v8::String::NewFromUtf8(isolate, ctx_->message.c_str()).ToLocalChecked());
            break;
        case Type::RangeError:
            exception =
                v8::Exception::RangeError(v8::String::NewFromUtf8(isolate, ctx_->message.c_str()).ToLocalChecked());
            break;
        case Type::ReferenceError:
            exception =
                v8::Exception::ReferenceError(v8::String::NewFromUtf8(isolate, ctx_->message.c_str()).ToLocalChecked());
            break;
        case Type::SyntaxError:
            exception =
                v8::Exception::SyntaxError(v8::String::NewFromUtf8(isolate, ctx_->message.c_str()).ToLocalChecked());
            break;
        case Type::TypeError:
            exception =
                v8::Exception::TypeError(v8::String::NewFromUtf8(isolate, ctx_->message.c_str()).ToLocalChecked());
            break;
        }
    }
    ctx_->error = v8::Global<v8::Value>(isolate, exception);
}


} // namespace jspp