#pragma once
#include "Fwd.h"
#include "MetaInfo.h"
#include "NativeInstance.h"

#include <memory>


namespace v8kit {
struct ClassMeta;
}
namespace v8kit {


/**
 * 实例内部数据载体
 * @note 此结构作为 C++ 侧资源承载体，存储进 v8 InternalField 中
 */
struct InstancePayload final {
private:
    std::unique_ptr<NativeInstance> holder_;

    // internal use only
    ClassMeta const* define_{nullptr};
    Engine const*    engine_{nullptr};
    bool const       constructFromJs_{false};

    friend Engine; // for internal use only


public:
    [[nodiscard]] inline NativeInstance& getHolder() { return *holder_; }

    [[nodiscard]] inline ClassMeta const* getDefine() const { return define_; }

    [[nodiscard]] bool isConstructFromJs() const { return constructFromJs_; }

    template <typename T>
    inline T* unwrap() const {
        return holder_->unwrap<T>();
    }

    V8KIT_DISABLE_COPY(InstancePayload);

    explicit InstancePayload() = delete;
    explicit InstancePayload(
        std::unique_ptr<NativeInstance>&& holder,
        ClassMeta const*                  define,
        Engine const*                     engine,
        bool                              constructFromJs
    )
    : holder_(std::move(holder)),
      define_(define),
      engine_(engine),
      constructFromJs_(constructFromJs) {}

    ~InstancePayload() = default;
};


} // namespace v8kit