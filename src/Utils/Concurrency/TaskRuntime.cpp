#include "TaskRuntime.hpp"

#include <algorithm>
#include <atomic>
#include <memory>
#include <mutex>
#include <taskflow/taskflow.hpp>
#include <thread>
#include <unordered_map>
#include <vector>

#include "Utils/Logger/Logger.hpp"

namespace {
using namespace std::chrono_literals;

struct TaskRecord {
    std::shared_ptr<std::atomic_bool> cancelled;
    std::future<void> future;
    std::string name;
};

std::once_flag gInitFlag;
std::unique_ptr<tf::Executor> gExecutor;
std::atomic_bool gStopping{false};
std::atomic_uint64_t gNextTaskId{1};
std::mutex gTaskMutex;
std::unordered_map<TaskRuntime::TaskId, TaskRecord> gTasks;

constexpr auto kCancelCheckSlice = 25ms;

tf::Executor& getExecutor() {
    return *gExecutor;
}

void cleanupFinishedLocked() {
    for (auto it = gTasks.begin(); it != gTasks.end();) {
        if (it->second.future.valid() &&
            it->second.future.wait_for(std::chrono::milliseconds::zero()) == std::future_status::ready) {
            it = gTasks.erase(it);
            continue;
        }
        ++it;
    }
}

bool sleepWithCancel(
    const std::chrono::milliseconds duration,
    const std::shared_ptr<std::atomic_bool>& cancelToken
) {
    auto deadline = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < deadline) {
        if (gStopping.load(std::memory_order_relaxed) || cancelToken->load(std::memory_order_relaxed)) {
            return false;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        std::this_thread::sleep_for(std::min(remaining, kCancelCheckSlice));
    }
    return !gStopping.load(std::memory_order_relaxed) && !cancelToken->load(std::memory_order_relaxed);
}

} // namespace

void TaskRuntime::initialize(std::size_t workerCount) {
    std::call_once(gInitFlag, [workerCount]() {
        gExecutor = workerCount == 0
            ? std::make_unique<tf::Executor>()
            : std::make_unique<tf::Executor>(workerCount);
    });
}

void TaskRuntime::submitVoid(std::function<void()> task) {
    initialize();
    getExecutor().async(std::move(task));
}

bool TaskRuntime::isStopping() {
    return gStopping.load(std::memory_order_relaxed);
}

TaskRuntime::TaskId TaskRuntime::scheduleDetached(std::function<void()> task, std::string name) {
    initialize();
    auto cancelToken = std::make_shared<std::atomic_bool>(false);

    auto wrapped = [cancelToken, task = std::move(task)]() mutable {
        if (gStopping.load(std::memory_order_relaxed) || cancelToken->load(std::memory_order_relaxed)) {
            return;
        }

        try {
            task();
        } catch (const std::exception& e) {
            Logger::warn("TaskRuntime detached task failed: {}", e.what());
        } catch (...) {
            Logger::warn("TaskRuntime detached task failed with unknown exception");
        }
    };

    auto future = getExecutor().async(std::move(wrapped));
    std::lock_guard<std::mutex> lock(gTaskMutex);
    cleanupFinishedLocked();
    const auto taskId = gNextTaskId.fetch_add(1, std::memory_order_relaxed);
    gTasks.emplace(taskId, TaskRecord{cancelToken, std::move(future), std::move(name)});
    return taskId;
}

TaskRuntime::TaskId TaskRuntime::schedulePeriodic(
    std::function<void()> task,
    const std::chrono::milliseconds interval,
    const std::chrono::milliseconds initialDelay,
    std::string name
) {
    initialize();
    auto cancelToken = std::make_shared<std::atomic_bool>(false);

    auto wrapped = [cancelToken, task = std::move(task), interval, initialDelay]() mutable {
        if (initialDelay > std::chrono::milliseconds::zero() && !sleepWithCancel(initialDelay, cancelToken)) {
            return;
        }

        while (!gStopping.load(std::memory_order_relaxed) && !cancelToken->load(std::memory_order_relaxed)) {
            try {
                task();
            } catch (const std::exception& e) {
                Logger::warn("TaskRuntime periodic task failed: {}", e.what());
            } catch (...) {
                Logger::warn("TaskRuntime periodic task failed with unknown exception");
            }

            if (interval <= std::chrono::milliseconds::zero()) {
                break;
            }

            if (!sleepWithCancel(interval, cancelToken)) {
                break;
            }
        }
    };

    auto future = getExecutor().async(std::move(wrapped));
    std::lock_guard<std::mutex> lock(gTaskMutex);
    cleanupFinishedLocked();
    const auto taskId = gNextTaskId.fetch_add(1, std::memory_order_relaxed);
    gTasks.emplace(taskId, TaskRecord{cancelToken, std::move(future), std::move(name)});
    return taskId;
}

TaskRuntime::TaskId TaskRuntime::scheduleLoop(
    std::function<bool()> loopBody,
    const std::chrono::milliseconds interval,
    const std::chrono::milliseconds initialDelay,
    std::string name
) {
    initialize();
    auto cancelToken = std::make_shared<std::atomic_bool>(false);

    auto wrapped = [cancelToken, loopBody = std::move(loopBody), interval, initialDelay]() mutable {
        if (initialDelay > std::chrono::milliseconds::zero() && !sleepWithCancel(initialDelay, cancelToken)) {
            return;
        }

        while (!gStopping.load(std::memory_order_relaxed) && !cancelToken->load(std::memory_order_relaxed)) {
            bool keepRunning = false;
            try {
                keepRunning = loopBody();
            } catch (const std::exception& e) {
                Logger::warn("TaskRuntime loop task failed: {}", e.what());
                break;
            } catch (...) {
                Logger::warn("TaskRuntime loop task failed with unknown exception");
                break;
            }

            if (!keepRunning || interval <= std::chrono::milliseconds::zero()) {
                break;
            }

            if (!sleepWithCancel(interval, cancelToken)) {
                break;
            }
        }
    };

    auto future = getExecutor().async(std::move(wrapped));
    std::lock_guard<std::mutex> lock(gTaskMutex);
    cleanupFinishedLocked();
    const auto taskId = gNextTaskId.fetch_add(1, std::memory_order_relaxed);
    gTasks.emplace(taskId, TaskRecord{cancelToken, std::move(future), std::move(name)});
    return taskId;
}

void TaskRuntime::cancelTask(const TaskId id) {
    std::lock_guard<std::mutex> lock(gTaskMutex);
    auto it = gTasks.find(id);
    if (it != gTasks.end() && it->second.cancelled) {
        it->second.cancelled->store(true, std::memory_order_relaxed);
    }
    cleanupFinishedLocked();
}

void TaskRuntime::cancelAll() {
    std::lock_guard<std::mutex> lock(gTaskMutex);
    for (auto& [_, task] : gTasks) {
        if (task.cancelled) {
            task.cancelled->store(true, std::memory_order_relaxed);
        }
    }
}

void TaskRuntime::waitForIdle() {
    initialize();
    getExecutor().wait_for_all();

    std::lock_guard<std::mutex> lock(gTaskMutex);
    cleanupFinishedLocked();
}

void TaskRuntime::shutdown(const std::chrono::milliseconds timeout) {
    initialize();
    gStopping.store(true, std::memory_order_relaxed);

    std::vector<std::future<void>> futures;
    {
        std::lock_guard<std::mutex> lock(gTaskMutex);
        for (auto& [_, task] : gTasks) {
            if (task.cancelled) {
                task.cancelled->store(true, std::memory_order_relaxed);
            }
            futures.emplace_back(std::move(task.future));
        }
        gTasks.clear();
    }

    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (auto& future : futures) {
        if (!future.valid()) {
            continue;
        }

        const auto now = std::chrono::steady_clock::now();
        const auto remaining = now < deadline
            ? std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now)
            : std::chrono::milliseconds::zero();
        if (future.wait_for(remaining) == std::future_status::timeout) {
            Logger::warn("TaskRuntime shutdown timed out waiting for task completion");
        }
    }

    getExecutor().wait_for_all();
}
