#pragma once
#include "v8kit/Macro.h"

V8KIT_WARNING_GUARD_BEGIN
#include <v8-context.h>
#include <v8-isolate.h>
#include <v8-locker.h>
#include <v8.h>
V8KIT_WARNING_GUARD_END


namespace v8kit {

class Engine;
class NativeInstance;

class EngineScope final {
public:
    explicit EngineScope(Engine& runtime);
    explicit EngineScope(Engine* runtime);
    ~EngineScope();

    V8KIT_DISABLE_COPY_MOVE(EngineScope);
    V8KIT_DISABLE_NEW();

    static Engine* currentEngine();

    static Engine& currentEngineChecked();

    static std::tuple<v8::Isolate*, v8::Local<v8::Context>> currentIsolateAndContextChecked();

    static v8::Isolate* currentEngineIsolateChecked();

    static v8::Local<v8::Context> currentEngineContextChecked();

private:
    static void ensureEngine(Engine* engine);

    // 作用域链
    Engine const* engine_{nullptr};
    EngineScope*  prev_{nullptr};

    // v8作用域
    v8::Locker         locker_;
    v8::Isolate::Scope isolateScope_;
    v8::HandleScope    handleScope_;
    v8::Context::Scope contextScope_;

    static thread_local EngineScope* gCurrentScope_;
};

class ExitEngineScope final {
    v8::Unlocker unlocker_;

public:
    explicit ExitEngineScope();
    ~ExitEngineScope() = default;

    V8KIT_DISABLE_COPY_MOVE(ExitEngineScope);
    V8KIT_DISABLE_NEW();
};


/**
 * @brief 瞬态对象作用域
 * 此作用域用于解决 C++ 中一些回调场景，如 `onEvent(std::function<void(EventBase const&)>)` 回调函数
 * 在 C++ 中，onEvent 显式的约定了 EventBase 参数不可修改、仅当前线程访问，不支持异步消费
 * 但如果我们将 onEvent 绑定给脚本，在 JavaScript 世界中，异步、闭包逃逸是常态，我们无法保证 JavaScript 会同步处理此回调
 * 如果 JavaScript 进行了闭包逃逸或者异步回调，在后续访问此资源时会出现 UAF，进而导致宿主崩溃
 * 因此为了解决 ReturnValuePolicy::kReference 在回调场景下的安全问题，设计了 `TransientObjectScope`
 *
 * TransientObjectScope 的设计思想是，在进行 onEvent 触发后，在当前栈上创建一个 `TransientObjectScope enter{}`
 * 在 `enter` 作用域内，所有 ReturnValuePolicy 为 kReference 的 NativeInstance 都会被 `TransientObjectScope` 跟踪
 * 当通知脚本回调后，脚本需要立刻处理，如果脚本进行异步回调或者闭包逃逸，在 `enter` 作用域结束后，所有被跟踪的
 * NativeInstance 都会执行 `invalidate` 操作将资源清理，当脚本异步执行（或者访问逃逸资源）时 v8kit 会在 unwrap
 * 时抛出异常，从而避免 UAF
 *
 * @note 如果一个资源是明确长期有效的，您可以使用 kReferencePersistent 策略，详情见 ReturnValuePolicy 注释
 */
class TransientObjectScope final {
public:
    V8KIT_DISABLE_NEW();
    V8KIT_DISABLE_COPY_MOVE(TransientObjectScope);

    TransientObjectScope();
    ~TransientObjectScope();

    void track(NativeInstance* instance);

    static bool isActive();

    static TransientObjectScope* current();

    static TransientObjectScope& currentChecked();

private:
    std::vector<NativeInstance*> trackedInstances_;
    TransientObjectScope*        prev_{nullptr};

    static thread_local TransientObjectScope* gCurrentScope_;
};

namespace internal {

class V8EscapeScope final {
    v8::EscapableHandleScope handleScope_;

public:
    explicit V8EscapeScope();
    explicit V8EscapeScope(v8::Isolate* isolate);
    ~V8EscapeScope() = default;

    template <typename T>
    v8::Local<T> escape(v8::Local<T> value) {
        return handleScope_.Escape(value);
    }
};

} // namespace internal

} // namespace v8kit