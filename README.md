# jspp 🚀

**[English](./README.md)** | **[简体中文](./README_ZH.md)**

---

**jspp** is a modern C++ wrapper for the Google V8 engine. Inspired by `pybind11`, it aims to eliminate the massive
boilerplate required when integrating C++ with V8.

With jspp, you can expose C++ classes, standard library containers, and smart pointers to JavaScript safely and
fluently without dealing with V8's complex underlying APIs (like `v8::Isolate`, `v8::Local`, or internal field slots)
manually.

## ✨ Features

- **Multiple backend support**: Supports `V8` and `QuickJS` engines, with plans to support more backends in the future.
- **pybind11-style Fluent API**: Declare JS bindings in pure, clean C++ using `defClass` and `defEnum`.
- **Seamless Type Conversions**: Out-of-the-box support for `std::vector`, `std::unordered_map`, `std::optional`,
  `std::variant`, `std::string`, and more.
- **Smart Pointers & Lifetimes**: Full support for `std::shared_ptr`, `std::unique_ptr`, `std::weak_ptr`.
- **Advanced Memory Management**: Powerful `ReturnValuePolicy` system (kCopy, kReference, kTakeOwnership,
  kReferenceInternal) to prevent memory leaks and Use-After-Free.
- **OOP Support**: Native support for single/multiple inheritance, polymorphism (RTTI downcasting), and
  interface/abstract classes.
- **Callback Safety**: `std::function` mapping with `TransientObjectScope` ensures safe JS-to-C++ callbacks without
  closure escape crashes.

## 🚀 Quick Start

Here's how easily you can bind a C++ class to V8:

```cpp
#include "jspp/core/Engine.h"
#include "jspp/core/EngineScope.h"
#include "jspp/binding/MetaBuilder.h"

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
using namespace jspp::binding;
auto PetMeta = defClass<Pet>("Pet")
    .ctor<std::string>()
    .prop("name", &Pet::getName, &Pet::setName)
    .method("bark", &Pet::bark)
    .build();

// 3. Run it in the Engine!
int main() {
    jspp::Engine engine;
    jspp::EngineScope scope(engine);

    // Register the bound class
    engine.registerClass(PetMeta);

    // Execute JavaScript code
    auto result = engine.eval(jspp::String::newString(R"(
        let dog = new Pet("Buddy");
        dog.name = "Max"; // Triggers setName
        dog.bark(3);      // Returns "Max barked 3 times!"
    )"));

    std::cout << result.asString().getValue() << std::endl;
    return 0;
}
```

## 🧠 Advanced: Return Value Policies

Just like pybind11, jspp provides fine-grained control over object lifetimes when passing C++ objects to JavaScript:

- `kAutomatic` (Default)
- `kReference` (C++ manages memory, JS just holds a reference)
- `kCopy` (JS gets a copied instance)
- `kTakeOwnership` (JS GC will delete the C++ object)
- `kReferenceInternal` (Ties the child's lifetime to the parent's lifetime)

## 🔨 Building

jspp is built with CMake. To build, run:

### Build jspp only

> **Note**: You need provide the path to your V8 include directory.

```sh
mkdir build
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Release \
    -DJSPP_EXTERNAL_INC=/path/to/v8/include

cmake --build build --config Release
```

### Build jspp with testing

> **Note**: You need to build **v8 monolith lib** and provide the path to it.

```sh
mkdir build
cmake -B build -S . \
    -DCMAKE_BUILD_TYPE=Debug \
    -DJSPP_EXTERNAL_INC=/path/to/v8/include \
    -DJSPP_EXTERNAL_LIB=/path/to/v8/v8_monolith.a \
    -DJSPP_BUILD_TESTS=ON

cmake --build build --config Debug
```
