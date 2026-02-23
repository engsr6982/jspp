## v8kit 🚀

**v8kit** 是一个基于现代化 C++ 开发的 Google V8 引擎封装库。它的设计深度借鉴了 `pybind11`，旨在彻底消除在 C++ 与 V8
集成时所需编写的繁琐“胶水代码”。

使用 v8kit，你无需直接面对 V8 复杂的底层概念（如各种 Handle、Isolate、Context 或内存槽），就能以极其优雅和安全的方式将 C++
类、STL 容器、智能指针暴露给 JavaScript。

### ✨ 核心特性

* **类似 pybind11 的链式 API**：使用 `defClass` 和 `defEnum` 极速声明绑定关系。
* **无缝类型转换**：开箱即用支持 `std::vector`, `std::unordered_map`, `std::optional`, `std::variant`, `std::string` 等。
* **智能指针与生命周期**：完美兼容 `std::shared_ptr`, `std::unique_ptr`, `std::weak_ptr`。
* **内存管理策略**：强大的 `ReturnValuePolicy` (返回值策略) 系统（如 kCopy, kReference, kTakeOwnership,
  kReferenceInternal），避免内存泄漏和 UAF（悬垂引用）。
* **面向对象支持**：原生支持继承链、多重继承、多态（RTTI 自动向下转型）及抽象类。
* **回调安全**：针对 `std::function` 提供了 `TransientObjectScope` 瞬态作用域保护，彻底阻断 JS 闭包逃逸导致的宿主崩溃问题。

### 🚀 快速开始

只需几行代码，即可将 C++ 类注册到 JS 世界：

```cpp
#include "v8kit/core/Engine.h"
#include "v8kit/core/EngineScope.h"
#include "v8kit/binding/MetaBuilder.h"

// 1. 定义你的 C++ 类
class Pet {
    std::string name;
public:
    Pet(std::string name) : name(std::move(name)) {}
    std::string getName() const { return name; }
    void setName(std::string n) { name = std::move(n); }
    std::string bark(int times) { return name + " barked " + std::to_string(times) + " times!"; }
};

// 2. 像 pybind11 一样进行绑定
using namespace v8kit::binding;
auto PetMeta = defClass<Pet>("Pet")
    .ctor<std::string>()
    .prop("name", &Pet::getName, &Pet::setName)
    .method("bark", &Pet::bark)
    .build();

// 3. 在引擎中运行！
int main() {
    v8kit::Engine engine;
    v8kit::EngineScope scope(engine);
    
    // 注册刚刚绑定的类
    engine.registerClass(PetMeta);
    
    // 执行 JavaScript 代码
    auto result = engine.eval(v8kit::String::newString(R"(
        let dog = new Pet("Buddy");
        dog.name = "Max"; // 自动调用 C++ 的 setName
        dog.bark(3);      // 返回 "Max barked 3 times!"
    )"));
    
    std::cout << result.asString().getValue() << std::endl;
    return 0;
}
```

### 🧠 进阶：返回值策略 (Return Value Policy)

同 pybind11 一样，当 C++ 向 JS 传递复杂对象时，v8kit 提供了精细的生命周期控制：

* `kAutomatic` (默认：根据引用、指针、值语义自动推导)
* `kReference` (C++ 保留所有权，JS 仅持有引用)
* `kCopy` (拷贝一个新的对象给 JS)
* `kTakeOwnership` (JS 接管所有权，当 JS 触发 GC 时会自动析构 C++ 对象)
* `kReferenceInternal` (子对象的生命周期与父对象绑定，保活机制)
