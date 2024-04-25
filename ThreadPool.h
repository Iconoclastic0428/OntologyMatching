#ifndef THREADPOOL_H
#define THREADPOOL_H

#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <unordered_map>
#include <string>
#include <vector>

class ThreadPool {
public:
    ThreadPool(size_t numThreads) {
        for (size_t i = 0; i < numThreads; ++i) {
            workers.emplace_back([this] {
                while (true) {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->queueMutex);
                        this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });

                        if (this->stop && this->tasks.empty()) return;

                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    task();  // Execute the task
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            stop = true;
        }

        condition.notify_all();
        for (std::thread& worker : workers) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    template<typename Func, typename... Args>
    auto enqueueTask(Func&& func, Args&&... args) -> std::future<decltype(func(args...))> {
        using return_type = decltype(func(std::forward<Args>(args)...));

        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<Func>(func), std::forward<Args>(args)...)
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queueMutex);

            if (stop) {
                throw std::runtime_error("Enqueue on stopped ThreadPool");
            }

            tasks.emplace([task]() { (*task)(); });
        }

        condition.notify_one();
        return res;
    }

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    std::mutex queueMutex;
    std::condition_variable condition;
    bool stop = false;
};

#endif
