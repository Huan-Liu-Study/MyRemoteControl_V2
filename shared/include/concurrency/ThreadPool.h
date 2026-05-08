#pragma once

#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <future>
#include <memory>
#include <stdexcept>

class ThreadPool {
private:
    std::vector<std::thread> workers;        //ThreadPool workers
    std::queue<std::function<void()>> tasks; // pending tasks
    std::mutex queue_mutex;                  // guards the task queue
    std::condition_variable condition;       // wakes workers when tasks arrive
    bool stop;                               // true means the pool is shutting down

public:
    // Create numThreads worker threads.
    ThreadPool(size_t numThreads) : stop(false) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this]() {
                while (true) {
                    std::function<void()> currentTask;

                    {
                        // Lock before touching shared data.
                        std::unique_lock<std::mutex> lock(queue_mutex);

                        // Keep waiting while there is no task and the pool is still running.
                        while (!stop && tasks.empty()) {
                            condition.wait(lock);
                        }

                        // Leave the thread when shutdown starts and no work is left.
                        if (stop && tasks.empty()) {
                            return;
                        }

                        // Take the next task out of the queue.
                        currentTask = std::move(tasks.front());
                        tasks.pop();
                    }

                    // Run the task after unlocking.
                    currentTask();
                }
            });
        }
    }

    // Add one task into the queue and return a future for its result.
    template<class F, class... Args>
    auto enqueue(F f, Args... args) -> std::future<decltype(f(args...))> {
        using ReturnType = decltype(f(args...));

        auto realJob = [=]() -> ReturnType {
            return f(args...);
        };

        auto taskPtr = std::make_shared<std::packaged_task<ReturnType()>>(realJob);
        std::future<ReturnType> result = taskPtr->get_future();

        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            if (stop) {
                throw std::runtime_error("ThreadPool has stopped.");
            }

            tasks.push([taskPtr]() {
                (*taskPtr)();
            });
        }

        condition.notify_one();
        return result;
    }

    // Stop the pool and wait for all worker threads to exit.
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }

        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
};
