#pragma once

#include <chrono>
#include <cstdint>
#include <future>
#include <functional>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>

class TaskRuntime {
public:
    using TaskId = std::uint64_t;

    static void initialize(std::size_t workerCount = 0);
    static void shutdown(std::chrono::milliseconds timeout = std::chrono::seconds(10));
    [[nodiscard]] static bool isStopping();

    static TaskId scheduleDetached(std::function<void()> task, std::string name = {});
    static TaskId schedulePeriodic(
        std::function<void()> task,
        std::chrono::milliseconds interval,
        std::chrono::milliseconds initialDelay = std::chrono::milliseconds::zero(),
        std::string name = {}
    );
    static TaskId scheduleLoop(
        std::function<bool()> loopBody,
        std::chrono::milliseconds interval,
        std::chrono::milliseconds initialDelay = std::chrono::milliseconds::zero(),
        std::string name = {}
    );

    static void cancelTask(TaskId id);
    static void cancelAll();
    static void waitForIdle();

    template <typename Fn>
    static auto submit(Fn&& task) -> std::future<std::invoke_result_t<Fn>> {
        using ReturnT = std::invoke_result_t<Fn>;
        auto packagedTask = std::make_shared<std::packaged_task<ReturnT()>>(std::forward<Fn>(task));
        auto future = packagedTask->get_future();

        submitVoid([packagedTask]() mutable {
            (*packagedTask)();
        });

        return future;
    }

private:
    static void submitVoid(std::function<void()> task);
};
