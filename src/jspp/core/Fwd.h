#pragma once
#include "jspp/Macro.h"

#include <functional>
#include <memory>

namespace jspp {

class Engine;
class Exception;
class EngineScope;
class ExitEngineScope;

enum class ValueKind : uint8_t;

class Value;
class Null;
class Undefined;
class Boolean;
class Number;
class BigInt;
class String;
class Symbol;
class Function;
class Object;
class Array;

class Arguments;

// handle

template <typename>
class Local;

template <typename>
class Global;

template <typename>
class Weak;


// scope
class EngineScope;
class ExitEngineScope;
class TransientObjectScope;
class StackFrameScope;

class Exception;
enum class ExceptionType {
    Unknown = -1, // JavaScript 侧抛出的异常为 Unknown
    Error,
    RangeError,
    ReferenceError,
    SyntaxError,
    TypeError
};


// attribute
enum class PropertyAttribute : uint32_t {
    /** None. **/
    None = 0,
    /** ReadOnly, i.e., not writable. **/
    ReadOnly = 1 << 0,
    /** DontEnum, i.e., not enumerable. **/
    DontEnum = 1 << 1,
    /** DontDelete, i.e., not configurable. **/
    DontDelete = 1 << 2
};

// todo: remove or implement it (need support multi backend)
// using PropertyDescriptor = v8::PropertyDescriptor;


// abstract layer
using FunctionCallback = std::function<Local<Value>(Arguments const& args)>;
using GetterCallback   = std::function<Local<Value>()>;
using SetterCallback   = std::function<void(Local<Value> const& value)>;

struct InstancePayload;
class NativeInstance;
using ConstructorCallback    = std::function<std::unique_ptr<NativeInstance>(Arguments const& args)>;
using InstanceMethodCallback = std::function<Local<Value>(InstancePayload&, Arguments const& args)>;
using InstanceGetterCallback = std::function<Local<Value>(InstancePayload&, Arguments const& args)>;
using InstanceSetterCallback = std::function<void(InstancePayload&, Arguments const& args)>;


namespace internal {

/**
 * Multi Backend Implementation
 */
template <typename T>
struct ImplType {
    // using type = <impl>;
};

} // namespace internal

template <typename E>
inline constexpr bool hasFlag(E e, E f) {
    using Underlying = std::underlying_type_t<E>;
    return (static_cast<Underlying>(e) & static_cast<Underlying>(f)) != 0;
}

} // namespace jspp

JSPP_DECL_FLAG_OPERATOR(jspp::PropertyAttribute)
