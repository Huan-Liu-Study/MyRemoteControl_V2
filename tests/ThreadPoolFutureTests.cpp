#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

#include "concurrency/ThreadPool.h"

namespace {

void expectTrue(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testFutureReturnsTaskResult() {
    ThreadPool pool(4);
    std::vector<std::future<int>> futures;

    auto start = std::chrono::steady_clock::now();

    for (int i = 0; i < 8; ++i) {
        futures.push_back(pool.enqueue([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            std::cout << "task " << i
                      << " running on thread "
                      << std::this_thread::get_id()
                      << std::endl;
            return i * i;
        }));
    }

    int sum = 0;
    for (auto& future : futures) {
        sum += future.get();
    }

    auto elapsed = std::chrono::steady_clock::now() - start;
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

    expectTrue(sum == 140, "future result sum mismatch");
    expectTrue(elapsedMs < 1600, "tasks should run concurrently");
}

void testFutureRethrowsTaskException() {
    ThreadPool pool(2);

    auto future = pool.enqueue([]() -> int {
        throw std::runtime_error("task failed");
    });

    bool caught = false;
    try {
        future.get();
    } catch (const std::runtime_error&) {
        caught = true;
    }

    expectTrue(caught, "future should rethrow task exception");
}

} // namespace

int main() {
    try {
        testFutureReturnsTaskResult();
        testFutureRethrowsTaskException();

        std::cout << "All thread pool future tests passed." << std::endl;
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Thread pool future test failed: " << ex.what() << std::endl;
        return 1;
    }
}
