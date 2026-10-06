#pragma once
#include <typeindex>

#include "Exception.h"

namespace jspp {

struct ClassMeta;
class enable_trampoline;

class NativeInstance {
protected:
    ClassMeta const* meta_;

public:
    explicit NativeInstance(ClassMeta const* meta) : meta_(meta) {}
    virtual ~NativeInstance() = default;

    JSPP_DISABLE_COPY(NativeInstance);

    [[nodiscard]] inline ClassMeta const* meta() const { return meta_; }

    virtual bool is_expired() const = 0;

    virtual void invalidate() = 0;

    virtual std::type_index type_id() const = 0;

    virtual bool is_const() const = 0;

    virtual void* cast(std::type_index target_type) const = 0;

    virtual std::shared_ptr<void> get_shared_ptr() const { return nullptr; }

    virtual std::unique_ptr<NativeInstance> clone() const = 0;

    virtual bool is_owned() const = 0;

    virtual void* release_ownership() = 0;

    virtual enable_trampoline* get_trampoline() const { return nullptr; }

    /**
     * Let this instance expire together with a parent instance.
     *
     * Used by kReferenceInternal member wrappers, which only hold a raw pointer into the
     * parent object's memory: after the parent is invalidated, reading the member must
     * throw instead of reading freed memory.
     *
     * @param parent Not owned. The child's JS wrapper keeps the parent wrapper alive, so
     *        the parent instance cannot be destroyed before the child instance.
     * @note The default implementation ignores the parent.
     */
    virtual void linkParent(NativeInstance const* parent) noexcept { (void)parent; }

    template <typename T>
    T* unwrap() const {
        if (is_expired()) {
            throw Exception{
                std::string{"Accessing destroyed instance of type "} + type_id().name(),
                Exception::Type::ReferenceError
            };
        }
        if (is_const() && !std::is_const_v<T>) {
            throw Exception("Cannot unwrap const instance to mutable pointer");
        }
        void* raw = cast(std::type_index(typeid(std::remove_cv_t<T>)));
        if (!raw) {
            throw Exception{
                std::string{"Cannot cast instance of type "} + type_id().name() + " to " + typeid(T).name(),
                Exception::Type::TypeError
            };
        }
        return static_cast<T*>(raw);
    }
};


} // namespace jspp