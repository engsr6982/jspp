#pragma once
#include "Reference.h"

namespace v8kit {

class Engine;

class ITrackedHandle {
    friend class Engine;

    ITrackedHandle* prev_   = nullptr;
    ITrackedHandle* next_   = nullptr;
    Engine*         engine_ = nullptr;

public:
    virtual ~ITrackedHandle();

    // 引擎死亡时的回调
    virtual void onEngineDestroy() = 0;

protected:
    void track(Engine* engine);
    void untrack();
};


template <typename T>
class TrackedGlobal final : public ITrackedHandle {
    Global<T> global_;

public:
    explicit TrackedGlobal(Local<T>&& local) {
        global_.reset(local);
        if (auto engine = global_.engine()) {
            this->track(engine);
        }
    }

    void onEngineDestroy() override { global_.reset(); }

    [[nodiscard]] Global<T>& global() { return global_; }

    [[nodiscard]] static std::shared_ptr<TrackedGlobal> create(Local<T>&& local) {
        return std::make_shared<TrackedGlobal>(std::move(local));
    }
};

template <typename T>
class TrackedWeak final : public ITrackedHandle {
    Weak<T> weak_;

public:
    explicit TrackedWeak(Local<T>&& weak) {
        weak_.reset(weak);
        if (auto engine = weak_.engine()) {
            this->track(engine);
        }
    }

    void onEngineDestroy() override { weak_.reset(); }

    [[nodiscard]] Weak<T>& weak() { return weak_; }

    [[nodiscard]] static std::shared_ptr<TrackedWeak> create(Local<T>&& local) {
        return std::make_shared<TrackedWeak>(std::move(local));
    }
};


} // namespace v8kit