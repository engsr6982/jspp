#pragma once
#include "Fwd.h"

#include <string>
#include <typeindex>

namespace v8kit {

struct StaticMemberMeta {
    struct Property {
        std::string const    name_;
        GetterCallback const getter_;
        SetterCallback const setter_;

        explicit Property(std::string name, GetterCallback getter, SetterCallback setter)
        : name_(std::move(name)),
          getter_(std::move(getter)),
          setter_(std::move(setter)) {}
    };

    struct Function {
        std::string const      name_;
        FunctionCallback const callback_;

        explicit Function(std::string name, FunctionCallback callback)
        : name_(std::move(name)),
          callback_(std::move(callback)) {}
    };

    std::vector<Property> const property_;
    std::vector<Function> const functions_;

    explicit StaticMemberMeta(std::vector<Property> property, std::vector<Function> functions)
    : property_(std::move(property)),
      functions_(std::move(functions)) {}
};

struct InstanceMemberMeta {
    struct Property {
        std::string const            name_;
        InstanceGetterCallback const getter_;
        InstanceSetterCallback const setter_;

        explicit Property(std::string name, InstanceGetterCallback getter, InstanceSetterCallback setter)
        : name_(std::move(name)),
          getter_(std::move(getter)),
          setter_(std::move(setter)) {}
    };

    struct Method {
        std::string const            name_;
        InstanceMethodCallback const callback_;

        explicit Method(std::string name, InstanceMethodCallback callback)
        : name_(std::move(name)),
          callback_(std::move(callback)) {}
    };

    ConstructorCallback const   constructor_;
    std::vector<Property> const property_;
    std::vector<Method> const   methods_;
    size_t const                classSize_{0}; // sizeof(C) for instance class

    // Type-erased dynamic copy/move constructors to preserve dynamic type
    using CopyCloneCtor = void* (*)(void const* src);
    using MoveCloneCtor = void* (*)(void* src);
    CopyCloneCtor const copyCloneCtor_{nullptr};
    MoveCloneCtor const moveCloneCtor_{nullptr};

    explicit InstanceMemberMeta(
        ConstructorCallback   constructor,
        std::vector<Property> property,
        std::vector<Method>   functions,
        size_t                classSize,
        CopyCloneCtor         copyCloneCtor = nullptr,
        MoveCloneCtor         moveCloneCtor = nullptr
    )
    : constructor_(std::move(constructor)),
      property_(std::move(property)),
      methods_(std::move(functions)),
      classSize_(classSize),
      copyCloneCtor_(copyCloneCtor),
      moveCloneCtor_(moveCloneCtor) {}
};

struct ClassMeta {
    std::string const        name_;
    StaticMemberMeta const   staticMeta_;
    InstanceMemberMeta const instanceMeta_;
    ClassMeta const*         base_;
    std::type_index const    typeId_;

    // 输入: Derived* (as void*)
    // 输出: Base* (as void*) - 指针地址可能发生变化
    using UpcasterCallback = void* (*)(void*);
    UpcasterCallback const upcaster_{nullptr}; // 转换到直接基类 (base_) 的函数

    /**
     * @brief 隐式接口/多继承放行映射表 (用于解除对 base_ 的强制依赖)
     * @note 这里的类型转换[仅对 C++ 类型系统可见]，不会影响 JavaScript 的原型链 (prototype)。
     *       当 C++ 侧需要将一个 JS 对象 Unwrap 为某个基类或接口时，会优先从这里查找转换函数，完成指针偏移。
     */
    std::unordered_map<std::type_index, UpcasterCallback> const extraCasters_;

    [[nodiscard]] void* castTo(void* ptr, std::type_index targetId) const {
        if (typeId_ == targetId) return ptr;

        // 优先检查隐式接口放行表，执行安全的指针向上转型 (Upcast)
        if (auto it = extraCasters_.find(targetId); it != extraCasters_.end()) {
            return it->second(ptr);
        }

        // 递归向单继承原型链 (JS Prototype Chain 映射) 查找
        if (base_ && upcaster_) {
            // 先转为 Base*
            void* basePtr = upcaster_(ptr);
            // 在基类中继续查找
            return base_->castTo(basePtr, targetId);
        }
        return nullptr;
    }

    [[nodiscard]] inline bool hasConstructor() const { return instanceMeta_.constructor_ != nullptr; }

    [[nodiscard]] inline bool isA(std::type_index typeIdx, bool recursion = true) const {
        if (typeId_ == typeIdx) return true;
        if (extraCasters_.contains(typeIdx)) return true; // 兼容隐式接口检查
        if (!recursion || !base_) return false;
        ClassMeta const* curr = base_;
        while (curr != nullptr) {
            if (curr->typeId_ == typeIdx) return true;
            if (curr->extraCasters_.contains(typeIdx)) return true; // 祖先类实现的接口同样有效
            curr = curr->base_;
        }
        return false;
    }

    [[nodiscard]] inline bool isA(ClassMeta const& meta, bool recursion = true) const {
        return isA(meta.typeId_, recursion);
    }

    template <typename T>
    [[nodiscard]] inline bool isA(bool recursion = true) const {
        return isA(std::type_index(typeid(T)), recursion);
    }

    explicit ClassMeta(
        std::string                                           name,
        StaticMemberMeta                                      staticMeta,
        InstanceMemberMeta                                    instanceMeta,
        ClassMeta const*                                      base,
        std::type_index                                       typeId,
        UpcasterCallback                                      upcaster     = nullptr,
        std::unordered_map<std::type_index, UpcasterCallback> extraCasters = {}
    )
    : name_(std::move(name)),
      staticMeta_(std::move(staticMeta)),
      instanceMeta_(std::move(instanceMeta)),
      base_(base),
      typeId_(typeId),
      upcaster_(upcaster),
      extraCasters_(std::move(extraCasters)) {}
};

struct EnumMeta {
public:
    struct Entry {
        std::string const name_;
        int64_t const     value_;

        explicit Entry(std::string name, int64_t value) : name_(std::move(name)), value_(value) {}
    };

    std::string const        name_;
    std::vector<Entry> const entries_;

    explicit EnumMeta(std::string name, std::vector<Entry> entries)
    : name_(std::move(name)),
      entries_(std::move(entries)) {}
};


} // namespace v8kit
