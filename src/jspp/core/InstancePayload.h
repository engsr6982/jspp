#pragma once
#include "Fwd.h"
#include "MetaInfo.h"
#include "NativeInstance.h"

#include <memory>


namespace jspp {
struct ClassMeta;
}
namespace jspp {


/**
 * 实例内部数据载体
 * @note 此结构作为 C++ 侧资源承载体
 */
struct InstancePayload final {
private:
    std::unique_ptr<NativeInstance> holder_;

    // internal use only
    ClassMeta const* define_{nullptr};
    Engine*          engine_{nullptr};
    bool const       constructFromJs_{false};

    friend Engine; // for internal use only


public:
    [[nodiscard]] inline Engine* getEngine() const { return engine_; }

    [[nodiscard]] inline NativeInstance& getHolder() const { return *holder_; }

    [[nodiscard]] inline ClassMeta const* getDefine() const { return define_; }

    [[nodiscard]] inline bool isConstructFromJs() const { return constructFromJs_; }

    template <typename T>
    inline T* unwrap() const {
        return holder_->unwrap<T>();
    }

    JSPP_DISABLE_COPY(InstancePayload);

    explicit InstancePayload() = delete;
    explicit InstancePayload(
        std::unique_ptr<NativeInstance>&& holder,
        ClassMeta const*                  define,
        Engine*                           engine,
        bool                              constructFromJs
    )
    : holder_(std::move(holder)),
      define_(define),
      engine_(engine),
      constructFromJs_(constructFromJs) {}

    ~InstancePayload() = default;
};


} // namespace jspp