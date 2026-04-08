#pragma once
#include "Fwd.h"
#include "jspp/Macro.h"
#include "jspp/core/Concepts.h"

#include "jspp-backend/traits/TraitScope.h"

#include <optional>

namespace jspp {

class Engine;
class NativeInstance;

class EngineScope final {
public:
    explicit EngineScope(Engine& engine);
    explicit EngineScope(Engine* engine);
    ~EngineScope();

    JSPP_DISABLE_COPY_MOVE(EngineScope);
    JSPP_DISABLE_NEW();

    static Engine* currentEngine();

    static Engine& currentEngineChecked();

private:
    struct InternalEnterFlag {};
    explicit EngineScope(InternalEnterFlag, Engine* engine, bool needEnter = true);

    friend class ExitEngineScope;

    static void ensureEngine(Engine* engine);

    bool needEnter_{false}; // 是否需要进入作用域

    using BakcendImpl = internal::ImplType<EngineScope>::type;
    std::optional<BakcendImpl> impl_;

    // 作用域链
    Engine*      engine_{nullptr};
    EngineScope* prev_{nullptr};

    static thread_local EngineScope* gCurrentScope_;
};

class ExitEngineScope final {
    using BackendImpl = internal::ImplType<ExitEngineScope>::type;
    struct ExitHolder {
        std::optional<BackendImpl> impl_;
        explicit ExitHolder(Engine* engine);
    };

    ExitHolder  holder_;
    EngineScope nullScope_;

public:
    explicit ExitEngineScope();
    ~ExitEngineScope();

    JSPP_DISABLE_COPY_MOVE(ExitEngineScope);
    JSPP_DISABLE_NEW();
};

/**
 * @brief 栈帧作用域 (抽象内存屏障)
 *
 * StackFrameScope 用于模拟 C++ 函数调用的栈帧行为，主要解决脚本句柄（Handles）在循环或长函数中的堆积问题。
 *
 * @section 核心职责
 * 1. 内存屏障：在作用域内生成的局部脚本对象句柄会在析构时被批量回收。
 * 2. 值逃逸：允许将一个特定的局部句柄“逃逸”（Escape）到父级作用域，防止其随当前栈帧一同销毁。
 *
 * @section 引擎差异实现
 * - V8 后端：封装 `v8::EscapableHandleScope`。在 V8 中，如果不显式开启新的作用域而在循环中创建大量 Local 句柄，
 *   这些句柄会持续堆积在当前的 HandleScope 中直至函数结束，造成极大的内存压力甚至导致堆栈溢出。
 *   通过 `StackFrameScope` 可以实现“即用即切”，在循环体内部精细回收。
 * - QuickJS/其他后端：对于基于引用计数或无显式句柄池的引擎，此类通常作为 NOP (无操作) 实现，
 *   或者仅作为逻辑上的代码边界，其 `escape` 方法仅执行简单的指针/引用转发。
 */
class StackFrameScope final {
    using BackendImpl = internal::ImplType<StackFrameScope>::type;
    BackendImpl impl_;

public:
    JSPP_DISABLE_NEW();
    JSPP_DISABLE_COPY_MOVE(StackFrameScope);

    explicit StackFrameScope(Engine& engine);
    ~StackFrameScope();

    template <concepts::WrapType T>
    [[nodiscard]] Local<T> escape(Local<T> handle) {
        return impl_.escape(std::move(handle));
    }
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
 * NativeInstance 都会执行 `invalidate` 操作将资源清理，当脚本异步执行（或者访问逃逸资源）时 jspp 会在 unwrap
 * 时抛出异常，从而避免 UAF
 *
 * @note 如果一个资源是明确长期有效的，您可以使用 kReferencePersistent 策略，详情见 ReturnValuePolicy 注释
 */
class TransientObjectScope final {
public:
    JSPP_DISABLE_NEW();
    JSPP_DISABLE_COPY_MOVE(TransientObjectScope);

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

} // namespace jspp