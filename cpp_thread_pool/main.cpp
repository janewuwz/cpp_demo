
#ifndef thread_pool_hpp
#define thread_pool_hpp

#include <stdio.h>

#include <iostream>
#include <thread>
#include <chrono>
#include <future>
#include <string>
#include <vector>
#include <queue>
#include <memory>
#include <functional>

class ThreadPool {
public:
    ThreadPool(size_t threads);
    ~ThreadPool();
    
    template<typename F, typename... Args>
    auto enqueue(F &&f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type>;
    
private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    
    std::mutex queue_mutex;
    std::condition_variable condition;
    
    bool stop;
};

inline ThreadPool::ThreadPool(size_t threads): stop(false) {
    for (size_t i = 0; i < threads; ++i) {
        workers.emplace_back([this] {
            for (;;) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(this->queue_mutex);
                    
                    this->condition.wait(lock, [this] {
                        return this->stop || !this->tasks.empty();
                    });
                    
                    if (this->stop && this->tasks.empty()) return;
                    
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }
                task();
            }
        });
    }
}

inline ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    
    condition.notify_all();
    
    for (std::thread &worker: workers)
        worker.join();
}


template <typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args)
->std::future<typename std::result_of<F(Args...)>::type> {
    
    using return_type = typename std::result_of<F((Args...))>::type;
    
    auto task = std::make_shared<std::packaged_task<return_type()>>
    (std::bind(std::forward<F>(f), std::forward<Args>(args)...));
    
    std::future<return_type> res = task->get_future();
    
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        
        if (stop)
            throw std::runtime_error("enqueue on stopped ThreadPool");
        
        tasks.emplace([task]{(*task)();});
    }
    
    condition.notify_one();
    return res;
    
}

#endif /* thread_pool_hpp */


int main(int argc, const char * argv[]) {
    ThreadPool pool(4);
    std::vector<std::future<std::string>> results;
    
    for (int i = 0; i < 8; i++) {
        results.emplace_back(
                             pool.enqueue([i] {
            std::cout << "hello" << i << std::endl;
            std::this_thread::sleep_for(std::chrono::seconds(16 - i * 2));
            std::cout << "world" << i << std::endl;
            return std::string("---thread") + std::to_string(i) + std::string( "finished---");
        })
                             );
    }
    
    for (auto && result: results)
        std::cout << result.get() << ' ';
    std::cout << std::endl;
    return 0;
}
