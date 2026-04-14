#include "JobQueue.h"

namespace jspp::qjs_backend {


JobQueue::Job::Job(Callback cb, void* d, std::chrono::steady_clock::time_point dt)
: callback_(cb),
  data_(d),
  dueTime_(dt) {}

bool JobQueue::Job::operator>(const Job& other) const { return dueTime_ > other.dueTime_; }

JobQueue::JobQueue() : shutdown_(false) {}
JobQueue::~JobQueue() {
    shutdown(true);
    loopAndWait();
}

void JobQueue::postTask(Callback callback, void* data, int delayMs) {
    auto dueTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);

    std::lock_guard<std::mutex> lock(mutex_);
    tasks_.push({callback, data, dueTime});
    cv_.notify_one();
}

bool JobQueue::loopOnce() {
    std::vector<Job> dueTasks;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto                        now = std::chrono::steady_clock::now();

        while (!tasks_.empty() && tasks_.top().dueTime_ <= now) {
            dueTasks.push_back(tasks_.top());
            tasks_.pop();
        }
    }

    // execute tasks
    for (const auto& task : dueTasks) {
        task.callback_(task.data_);
    }

    return !dueTasks.empty();
}

void JobQueue::loopAndWait() {
    while (true) {
        if (shutdown_) {
            if (awaitTasks_ && !tasks_.empty()) {
                while (loopOnce()) {}
            }
            break;
        }
        if (!loopOnce()) {
            std::unique_lock<std::mutex> lock(mutex_);
            if (tasks_.empty() && !shutdown_) {
                cv_.wait_for(lock, std::chrono::milliseconds(100));
            }
        }
    }
}

void JobQueue::shutdown(bool wait) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_   = true;
        awaitTasks_ = wait;
    }
    cv_.notify_all();
}


} // namespace jspp::qjs_backend