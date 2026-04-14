#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>

namespace jspp::qjs_backend {

class JobQueue {
public:
    using Callback = void (*)(void* data);

    struct Job {
        Callback                              callback_; // 回调函数
        void*                                 data_;     // 数据指针
        std::chrono::steady_clock::time_point dueTime_;  // 执行时间

        Job(Callback cb, void* d, std::chrono::steady_clock::time_point dt);

        bool operator>(const Job& other) const;
    };

    JobQueue();
    ~JobQueue();

    void postTask(Callback callback, void* data = nullptr, int delayMs = 0);

    bool loopOnce();

    void loopAndWait();

    void shutdown(bool wait = false);

private:
    std::priority_queue<Job, std::vector<Job>, std::greater<>> tasks_;
    std::mutex                                                 mutex_;
    std::condition_variable                                    cv_;
    std::atomic<bool>                                          shutdown_;
    std::atomic<bool>                                          awaitTasks_;
};

} // namespace jspp::qjs_backend