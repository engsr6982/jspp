# v8kit 🚀

**[English](./README.md)** | **[简体中文](./README_ZH.md)**

---

**v8kit** is a modern C++ wrapper for the Google V8 engine. Inspired by `pybind11`, it aims to eliminate the massive
boilerplate required when integrating C++ with V8.

With v8kit, you can expose C++ classes, standard library containers, and smart pointers to JavaScript safely and
fluently without dealing with V8's complex underlying APIs (like `v8::Isolate`, `v8::Local`, or internal field slots)
manually.

### ✨ Features

* **pybind11-style Fluent API**: Declare JS bindings in pure, clean C++ using `defClass` and `defEnum`.
* **Seamless Type Conversions**: Out-of-the-box support for `std::vector`, `std::unordered_map`, `std::optional`,
  `std::variant`, `std::string`, and more.
* **Smart Pointers & Lifetimes**: Full support for `std::shared_ptr`, `std::unique_ptr`, `std::weak_ptr`.
* **Advanced Memory Management**: Powerful `ReturnValuePolicy` system (kCopy, kReference, kTakeOwnership,
  kReferenceInternal) to prevent memory leaks and Use-After-Free.
* **OOP Support**: Native support for single/multiple inheritance, polymorphism (RTTI downcasting), and
  interface/abstract classes.
* **Callback Safety**: `std::function` mapping with `TransientObjectScope` ensures safe JS-to-C++ callbacks without
  closure escape crashes.

### 🚀 Quick Start

Here's how easily you can bind a C++ class to V8:

```cpp
#include "v8kit/core/Engine.h"
#include "v8kit/core/EngineScope.h"
#include "v8kit/binding/MetaBuilder.h"

// 1. Define your C++ class
class Pet {
    std::string name;
public:
    Pet(std::string name) : name(std::move(name)) {}
    std::string getName() const { return name; }
    void setName(std::string n) { name = std::move(n); }
    std::string bark(int times) { return name + " barked " + std::to_string(times) + " times!"; }
};

// 2. Create bindings using pybind11-style API
using namespace v8kit::binding;
auto PetMeta = defClass<Pet>("Pet")
    .ctor<std::string>()
    .prop("name", &Pet::getName, &Pet::setName)
    .method("bark", &Pet::bark)
    .build();

// 3. Run it in the Engine!
int main() {
    v8kit::Engine engine;
    v8kit::EngineScope scope(engine);
    
    // Register the bound class
    engine.registerClass(PetMeta);
    
    // Execute JavaScript code
    auto result = engine.eval(v8kit::String::newString(R"(
        let dog = new Pet("Buddy");
        dog.name = "Max"; // Triggers setName
        dog.bark(3);      // Returns "Max barked 3 times!"
    )"));
    
    std::cout << result.asString().getValue() << std::endl;
    return 0;
}
```

### 🧠 Advanced: Return Value Policies

Just like pybind11, v8kit provides fine-grained control over object lifetimes when passing C++ objects to JavaScript:

* `kAutomatic` (Default)
* `kReference` (C++ manages memory, JS just holds a reference)
* `kCopy` (JS gets a copied instance)
* `kTakeOwnership` (JS GC will delete the C++ object)
* `kReferenceInternal` (Ties the child's lifetime to the parent's lifetime)
