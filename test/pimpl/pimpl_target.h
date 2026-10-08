#pragma once
#include <memory>

// pImpl 类的常见形状：
// - Impl 只在这里前置声明，定义在 pimpl_impl.cc 里；
// - 没有声明析构函数，所以它由编译器隐式生成，需要 Impl 的定义；
// 也就是说，包含这个头的 TU 无法构造/销毁 PimplTarget，只能通过引用或指针使用它。
class PimplTarget {
    struct Impl;
    std::unique_ptr<Impl> impl_;

public:
    PimplTarget();

    int  value() const;
    void setValue(int v);
};

/// 实例的构造与析构都留在 pimpl_impl.cc（Impl 可见），这里只返回引用
PimplTarget& pimplTarget();
