#pragma once
#include "ReturnValuePolicy.h"
#include "TypeConverter.h"

#include "jspp/binding/traits/TypeTraits.h"
#include "jspp/core/Concepts.h"
#include "jspp/core/MetaInfo.h"
#include "jspp/core/Utils.h"


#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>


namespace jspp::binding {


enum class ConstructorKind {
    kNone,    // 默认状态, 未设置状态
    kNormal,  // 默认绑定构造
    kCustom,  // 自定义/自行处理构造逻辑
    kDisabled // 禁止Js构造,自动生成空构造回调
};

template <typename T, ConstructorKind K = ConstructorKind::kNone>
class ClassMetaBuilder {
    std::string                               name_;
    std::vector<StaticMemberMeta::Property>   staticProperty_;
    std::vector<StaticMemberMeta::Function>   staticFunctions_;
    std::vector<InstanceMemberMeta::Property> instanceProperty_;
    std::vector<InstanceMemberMeta::Method>   instanceFunctions_;
    ClassMeta const*                          base_ = nullptr;

    ConstructorCallback              userDefinedConstructor_ = nullptr;
    std::vector<ConstructorCallback> constructors_           = {};

    ClassMeta::UpcasterCallback                                      upcaster_ = nullptr;
    std::unordered_map<std::type_index, ClassMeta::UpcasterCallback> extraCasters_;

    static constexpr bool isInstanceClass = !std::is_void_v<T>;

    template <ConstructorKind OtherState>
    explicit ClassMetaBuilder(ClassMetaBuilder<T, OtherState>&& other) noexcept
    : name_(std::move(other.name_)),
      staticProperty_(std::move(other.staticProperty_)),
      staticFunctions_(std::move(other.staticFunctions_)),
      instanceProperty_(std::move(other.instanceProperty_)),
      instanceFunctions_(std::move(other.instanceFunctions_)),
      base_(other.base_),
      userDefinedConstructor_(std::move(other.userDefinedConstructor_)),
      constructors_(std::move(other.constructors_)),
      upcaster_(other.upcaster_),
      extraCasters_(std::move(other.extraCasters_)) {
        // note: other may be in moved-from state
    }

    template <typename, ConstructorKind>
    friend class ClassMetaBuilder;

public:
    /**
     * @param name class name
     * @note support namespace like `a.b.c.ClassName`
     * @note class name cannot start or end with '.'
     */
    explicit constexpr ClassMetaBuilder(std::string_view name) : name_{name} {
        if (!namespace_utils::validNamespace(name)) {
            throw std::invalid_argument("Invalid class name: segments cannot be empty or consecutive dots");
        }
    }


    // -----------
    // static
    // -----------

    auto& func(std::string name, FunctionCallback fn) {
        staticFunctions_.emplace_back(std::move(name), std::move(fn));
        return *this;
    }

    template <typename Fn>
    auto& func(std::string name, Fn&& fn, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(!traits::isFunctionCallback_v<Fn>)
    {
        auto f = adapter::wrapFunction(std::forward<Fn>(fn), policy);
        staticFunctions_.emplace_back(std::move(name), std::move(f));
        return *this;
    }

    template <typename... Fn>
    auto& func(std::string name, Fn&&... fn)
        requires(sizeof...(Fn) > 1)
    {
        auto f = adapter::wrapOverloadFuncAndExtraPolicy(std::forward<Fn>(fn)...);
        staticFunctions_.emplace_back(std::move(name), std::move(f));
        return *this;
    }

    auto& var(std::string name, GetterCallback getter, SetterCallback setter) {
        staticProperty_.emplace_back(std::move(name), std::move(getter), std::move(setter));
        return *this;
    }

    template <typename G, typename S>
    auto& var(std::string name, G&& getter, S&& setter, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(!traits::isGetterCallback_v<G> && (!traits::isSetterCallback_v<S> || std::is_null_pointer_v<S>))
    {
        auto g = adapter::wrapGetter(std::forward<G>(getter), policy);
        auto s = adapter::wrapSetter(std::forward<S>(setter));
        staticProperty_.emplace_back(std::move(name), std::move(g), std::move(s));
        return *this;
    }

    template <typename Ty>
    auto& var(std::string name, Ty&& value, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(!traits::Callable<Ty>)
    {
        auto [g, s] = adapter::wrapStaticMember(std::forward<Ty>(value), policy);
        staticProperty_.emplace_back(std::move(name), std::move(g), std::move(s));
        return *this;
    }

    template <typename Ty>
    auto& var_readonly(std::string name, Ty&& value, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(!traits::Callable<Ty>)
    {
        auto [g, s] = adapter::wrapStaticMember<Ty, true>(std::forward<Ty>(value), policy);
        staticProperty_.emplace_back(std::move(name), std::move(g), std::move(s));
        return *this;
    }

    template <typename G>
    auto& var_readonly(std::string name, G&& getter, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(traits::Callable<G>)
    {
        if constexpr (traits::isGetterCallback_v<G>) {
            return var(std::move(name), std::forward<G>(getter), nullptr);
        } else {
            auto get = adapter::wrapGetter(std::forward<G>(getter), policy);
            staticProperty_.emplace_back(std::move(name), std::move(get), nullptr);
            return *this;
        }
    }


    // -----------
    // instance
    // -----------

    auto ctor(std::nullptr_t)
        requires(isInstanceClass && K == ConstructorKind::kNone)
    {
        userDefinedConstructor_ = [](Arguments const&) { return nullptr; };
        ClassMetaBuilder<T, ConstructorKind::kDisabled> builder{std::move(*this)};
        return builder; // NRVO/move
    }

    auto ctor(ConstructorCallback fn)
        requires(isInstanceClass && K == ConstructorKind::kNone)
    {
        userDefinedConstructor_ = std::move(fn);
        ClassMetaBuilder<T, ConstructorKind::kCustom> builder{std::move(*this)};
        return builder; // NRVO/move
    }

    template <typename... Args>
    decltype(auto) ctor()
        requires(isInstanceClass && (K == ConstructorKind::kNone || K == ConstructorKind::kNormal))
    {
        static_assert(std::is_constructible_v<T, Args...>, "Class must be constructible from Args...");
        static_assert(!std::is_aggregate_v<T>, "Binding ctor requires explicit constructor, not aggregate class");

        auto fn = adapter::wrapConstructor<T, Args...>();
        constructors_.emplace_back(std::move(fn));

        if constexpr (K == ConstructorKind::kNormal) {
            return *this;
        } else {
            ClassMetaBuilder<T, ConstructorKind::kNormal> builder{std::move(*this)};
            return builder; // NRVO/move
        }
    }

    /**
     * @brief 显式单继承绑定 (C++ -> JS)
     * @note 1. 会在 JavaScript 侧建立真实的 prototype 原型链关系 (类似 JS 中的 class A extends B)。
     * @note 2. 要求被继承的基类 `meta` 必须也是暴露给 JS 的。
     * @note 3. JavaScript 仅支持单继承，因此该方法只能调用一次。
     * @param meta 基类的元信息
     */
    template <typename P>
    auto& inherit(ClassMeta const& meta)
        requires isInstanceClass // 仅实例类允许继承
    {
        if (base_) { // 重复继承
            throw std::invalid_argument("class can only inherit one base class");
        }
        std::type_index type = typeid(P);
        if (meta.typeId_ != type) { // 拿错类元信息?
            throw std::invalid_argument("base class meta mismatch");
        }
        if (!meta.hasConstructor()) { // 父类是静态类
            throw std::invalid_argument("base class has no constructor");
        }
        static_assert(std::is_base_of_v<P, T>, "Illegal inheritance relationship");
        static_assert(!std::is_same_v<P, T>, "Identity inheritance is not allowed. Use multiple .ctor() calls.");

        base_     = &meta;
        upcaster_ = [](void* ptr) -> void* {
            T* derived = static_cast<T*>(ptr);
            P* base    = static_cast<P*>(derived);
            return base;
        };
        return *this;
    }

    /**
     * @brief 隐式接口实现 / 类型放行 (仅 C++ 侧可见)
     * @note 此方法用于向 jspp 类型系统声明 C++ 的继承关系，但不会修改 JavaScript 的原型链。
     *
     * @par 适用场景 1：隐藏 Trampoline (跳板类)
     * 如果你为了支持 JS 重写虚函数，定义了 `class JSPlugin : public Plugin, public TrampolineBase`，
     * 但你希望在 JS 侧直接暴露为 `class Plugin` 而非让 JS 看到奇怪的继承体系。
     * 此时调用 `.implements<Plugin>()` 即可让系统知道如何把 JS 对象安全转换回 `Plugin*`。
     *
     * @par 适用场景 2：支持 C++ 多继承与接口
     * JavaScript 只支持单继承。如果 C++ 类继承了多个接口 (例如 `class Entity : public Object, public IDamageable`)，
     * 你可以使用 `.inherit<Object>(...)` 建立主继承链，同时使用 `.implements<IDamageable>()`
     * 让需要 `IDamageable&` 的 C++ 函数能够正确接收并转换这个 JS 对象 (处理多继承指针偏移)。
     *
     * @tparam Target 需要向上转型的基类或接口类型
     */
    template <typename Target>
    auto& implements()
        requires isInstanceClass
    {
        static_assert(std::is_base_of_v<Target, T>, "Target must be a base class of T");
        extraCasters_[std::type_index(typeid(Target))] = [](void* ptr) -> void* {
            // 安全的指针偏移（处理多继承中的内存布局错位）
            return static_cast<Target*>(static_cast<T*>(ptr));
        };
        return *this;
    }

    auto& method(std::string name, InstanceMethodCallback fn)
        requires isInstanceClass
    {
        instanceFunctions_.emplace_back(std::move(name), std::move(fn));
        return *this;
    }

    template <typename Fn>
    auto& method(std::string name, Fn&& fn, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires isInstanceClass
    {
        auto f = adapter::wrapInstanceMethod<T>(std::forward<Fn>(fn), policy);
        instanceFunctions_.emplace_back(std::move(name), std::move(f));
        return *this;
    }

    template <typename... Fn>
    auto& method(std::string name, Fn&&... fn)
        requires(sizeof...(Fn) > 1 && isInstanceClass)
    {
        auto f = adapter::wrapOverloadMethodAndExtraPolicy<T>(std::forward<Fn>(fn)...);
        instanceFunctions_.emplace_back(std::move(name), std::move(f));
        return *this;
    }

    auto& prop(std::string name, InstanceGetterCallback getter, InstanceSetterCallback setter)
        requires isInstanceClass
    {
        instanceProperty_.emplace_back(std::move(name), std::move(getter), std::move(setter));
        return *this;
    }

    template <typename G, typename S>
    auto& prop(std::string name, G&& getter, S&& setter, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires isInstanceClass
    {
        auto r = adapter::wrapInstanceGetter<T>(std::forward<G>(getter), policy);
        auto w = adapter::wrapInstanceSetter<T>(std::forward<S>(setter));
        instanceProperty_.emplace_back(std::move(name), std::move(r), std::move(w));
        return *this;
    }

    template <typename Member>
    auto& prop(std::string name, Member member, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(isInstanceClass && std::is_member_object_pointer_v<Member>)
    {
        auto rw = adapter::wrapInstanceAccessor<T, false>(std::forward<Member>(member), policy);
        instanceProperty_.emplace_back(std::move(name), std::move(rw.first), std::move(rw.second));
        return *this;
    }

    template <typename Member>
    auto& prop_readonly(std::string name, Member member, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(isInstanceClass && std::is_member_object_pointer_v<Member>)
    {
        auto rw = adapter::wrapInstanceAccessor<T, true>(std::forward<Member>(member), policy);
        instanceProperty_.emplace_back(std::move(name), std::move(rw.first), std::move(rw.second));
        return *this;
    }

    template <typename G>
    auto& prop_readonly(std::string name, G&& getter, ReturnValuePolicy policy = ReturnValuePolicy::kAutomatic)
        requires(isInstanceClass && std::is_member_function_pointer_v<G>)
    {
        if constexpr (traits::isInstanceGetterCallback_v<G>) {
            return prop(std::move(name), std::forward<G>(getter), nullptr);
        } else {
            auto r = adapter::wrapInstanceGetter<T>(std::forward<G>(getter), policy);
            instanceProperty_.emplace_back(std::move(name), std::move(r), nullptr);
        }
        return *this;
    }

    [[nodiscard]] ClassMeta build() {
        ConstructorCallback constructorCallback = nullptr;
        if constexpr (isInstanceClass) {
            static_assert(K != ConstructorKind::kNone, "No constructor provided");
            if constexpr (K == ConstructorKind::kCustom || K == ConstructorKind::kDisabled) {
                constructorCallback = std::move(userDefinedConstructor_);
            } else { // Normal
                if (constructors_.empty()) {
                    throw std::invalid_argument("No constructor provided");
                }
                if (constructors_.size() == 1) {
                    // 没有重载构造函数，直接传递，避免无意义的尝试
                    constructorCallback = std::move(constructors_.front());
                } else {
                    constructorCallback =
                        [fn = std::move(constructors_)](Arguments const& arguments) -> std::unique_ptr<NativeInstance> {
                        for (auto const& f : fn) {
                            try {
                                if (auto holder = std::invoke(f, arguments)) {
                                    return std::move(holder);
                                }
                            } catch (Exception const&) {}
                        }
                        return nullptr;
                    };
                }
            }
        }

        InstanceMemberMeta::CopyCloneCtor copyCloneCtor = nullptr;
        InstanceMemberMeta::MoveCloneCtor moveCloneCtor = nullptr;
        if constexpr (isInstanceClass) {
            if constexpr (std::is_copy_constructible_v<T>) {
                copyCloneCtor = [](const void* src) -> void* {
                    // 把 void* 强转回确切的子类 T*，调用 T 的拷贝构造
                    return new T(*static_cast<const T*>(src));
                };
            }
            if constexpr (std::is_move_constructible_v<T>) {
                moveCloneCtor = [](void* src) -> void* { return new T(std::move(*static_cast<T*>(src))); };
            }
        }

        return ClassMeta{
            std::move(name_),
            StaticMemberMeta{std::move(staticProperty_), std::move(staticFunctions_)},
            InstanceMemberMeta{
                             std::move(constructorCallback),
                             std::move(instanceProperty_),
                             std::move(instanceFunctions_),
                             traits::size_of_v<T>,
                             copyCloneCtor, moveCloneCtor
            },
            base_,
            std::type_index{typeid(T)},
            upcaster_,
            std::move(extraCasters_)
        };
    }
};


template <typename T>
    requires std::is_enum_v<T>
class EnumMetaBuilder {
    std::string                  name_;
    std::vector<EnumMeta::Entry> entries_;

public:
    /**
     * @param name The name of the enum.
     * @note support namespace like `a.b.c.EnumName`
     * @note enum name cannot start or end with '.'
     */
    explicit constexpr EnumMetaBuilder(std::string_view name) : name_{name} {
        if (!namespace_utils::validNamespace(name)) {
            throw std::invalid_argument("Invalid enum name: segments cannot be empty or consecutive dots");
        }
    }

    EnumMetaBuilder& value(std::string name, T e) {
        entries_.emplace_back(std::move(name), static_cast<int64_t>(e));
        return *this;
    }

    [[nodiscard]] EnumMeta build() { return EnumMeta{std::move(name_), std::move(entries_)}; }
};


template <typename T>
auto defClass(std::string_view name) {
    return ClassMetaBuilder<T>{name};
}
template <typename T>
auto defEnum(std::string_view name) {
    return EnumMetaBuilder<T>{name};
}


} // namespace jspp::binding