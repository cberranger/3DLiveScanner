#ifndef THREAD_POOL_H
#define THREAD_POOL_H

/**
 * Thread Pool for 3D Live Scanner
 * 
 * Provides a reusable pool of worker threads to eliminate thread creation overhead.
 * Replaces raw pthread_create() calls with efficient task queuing.
 * 
 * Usage:
 *   ThreadPool pool(4);  // 4 worker threads
 *   auto future = pool.enqueue([](){ return heavy_computation(); });
 *   auto result = future.get();
 */

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <stdexcept>
#include <atomic>

namespace oc {

class ThreadPool {
public:
    /**
     * Create a thread pool with specified number of workers
     * @param numThreads Number of worker threads (default: hardware concurrency)
     */
    explicit ThreadPool(size_t numThreads = 0) : stop_(false), activeJobs_(0) {
        if (numThreads == 0) {
            numThreads = std::thread::hardware_concurrency();
            if (numThreads == 0) numThreads = 4;  // Fallback
        }
        
        workers_.reserve(numThreads);
        for (size_t i = 0; i < numThreads; ++i) {
            workers_.emplace_back([this] {
                workerLoop();
            });
        }
    }
    
    /**
     * Destructor - waits for all tasks to complete
     */
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }
    
    // Non-copyable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    
    /**
     * Enqueue a task and get a future for the result
     * 
     * @param f Callable to execute
     * @param args Arguments to pass to the callable
     * @return std::future for the result
     */
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        using return_type = typename std::result_of<F(Args...)>::type;
        
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );
        
        std::future<return_type> result = task->get_future();
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (stop_) {
                throw std::runtime_error("Cannot enqueue on stopped ThreadPool");
            }
            
            tasks_.emplace([task]() { (*task)(); });
        }
        
        condition_.notify_one();
        return result;
    }
    
    /**
     * Enqueue a task without waiting for result (fire-and-forget)
     * More efficient when you don't need the return value
     */
    template<class F, class... Args>
    void enqueueDetached(F&& f, Args&&... args) {
        auto task = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        
        {
            std::unique_lock<std::mutex> lock(mutex_);
            
            if (stop_) {
                return;  // Silently ignore if stopped
            }
            
            tasks_.emplace([task]() { task(); });
        }
        
        condition_.notify_one();
    }
    
    /**
     * Wait for all currently queued tasks to complete
     */
    void waitAll() {
        std::unique_lock<std::mutex> lock(mutex_);
        completionCondition_.wait(lock, [this] {
            return tasks_.empty() && activeJobs_ == 0;
        });
    }
    
    /**
     * Get number of pending tasks in queue
     */
    size_t pendingTasks() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return tasks_.size();
    }
    
    /**
     * Get number of worker threads
     */
    size_t numWorkers() const {
        return workers_.size();
    }
    
    /**
     * Check if pool is idle (no pending or active tasks)
     */
    bool isIdle() const {
        std::unique_lock<std::mutex> lock(mutex_);
        return tasks_.empty() && activeJobs_ == 0;
    }

private:
    void workerLoop() {
        while (true) {
            std::function<void()> task;
            
            {
                std::unique_lock<std::mutex> lock(mutex_);
                
                condition_.wait(lock, [this] {
                    return stop_ || !tasks_.empty();
                });
                
                if (stop_ && tasks_.empty()) {
                    return;
                }
                
                task = std::move(tasks_.front());
                tasks_.pop();
                ++activeJobs_;
            }
            
            // Execute task outside the lock
            task();
            
            {
                std::unique_lock<std::mutex> lock(mutex_);
                --activeJobs_;
                
                if (tasks_.empty() && activeJobs_ == 0) {
                    completionCondition_.notify_all();
                }
            }
        }
    }
    
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::condition_variable completionCondition_;
    
    bool stop_;
    std::atomic<int> activeJobs_;
};

/**
 * Global thread pool singleton for the application
 * Initialized with 4 threads (good for mobile big.LITTLE architectures)
 */
class GlobalThreadPool {
public:
    static ThreadPool& instance() {
        static ThreadPool pool(4);
        return pool;
    }
    
    // Convenience methods
    template<class F, class... Args>
    static auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>
    {
        return instance().enqueue(std::forward<F>(f), std::forward<Args>(args)...);
    }
    
    template<class F, class... Args>
    static void enqueueDetached(F&& f, Args&&... args) {
        instance().enqueueDetached(std::forward<F>(f), std::forward<Args>(args)...);
    }
    
    static void waitAll() {
        instance().waitAll();
    }
    
private:
    GlobalThreadPool() = delete;
};

} // namespace oc

#endif // THREAD_POOL_H
