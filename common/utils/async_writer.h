/**
 * async_writer.h - Asynchronous file writer with double buffering
 * 
 * Eliminates I/O stalls during scanning by writing files in a background thread.
 * Uses a ring buffer of write tasks to decouple file operations from the main loop.
 */

#ifndef ASYNC_WRITER_H
#define ASYNC_WRITER_H

#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>
#include <cstdio>
#include <cstring>

namespace oc {

/**
 * WriteTask represents a pending file write operation
 */
struct WriteTask {
    std::string filename;
    std::vector<uint8_t> data;
    bool binary;
    
    WriteTask() : binary(true) {}
    WriteTask(const std::string& fname, const void* ptr, size_t size, bool isBinary = true)
        : filename(fname), binary(isBinary) {
        data.resize(size);
        memcpy(data.data(), ptr, size);
    }
};

/**
 * AsyncWriter manages a background thread for non-blocking file writes
 */
class AsyncWriter {
public:
    static AsyncWriter& getInstance() {
        static AsyncWriter instance;
        return instance;
    }
    
    ~AsyncWriter() {
        shutdown();
    }
    
    /**
     * Queue a file write operation
     * Data is copied so the caller can immediately free/reuse the buffer
     */
    void write(const std::string& filename, const void* data, size_t size, bool binary = true) {
        if (!running_) return;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.emplace(filename, data, size, binary);
        }
        condition_.notify_one();
    }
    
    /**
     * Queue a write task (moves the task into the queue)
     */
    void write(WriteTask&& task) {
        if (!running_) return;
        
        {
            std::lock_guard<std::mutex> lock(mutex_);
            tasks_.push(std::move(task));
        }
        condition_.notify_one();
    }
    
    /**
     * Wait for all pending writes to complete
     * Call before shutdown or when data integrity is critical
     */
    void flush() {
        std::unique_lock<std::mutex> lock(mutex_);
        flushCondition_.wait(lock, [this] { 
            return tasks_.empty() && !currentlyWriting_; 
        });
    }
    
    /**
     * Get number of pending write tasks
     */
    size_t pendingCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return tasks_.size();
    }
    
    /**
     * Shutdown the writer thread (called automatically on destruction)
     */
    void shutdown() {
        if (!running_) return;
        
        running_ = false;
        condition_.notify_all();
        
        if (writerThread_.joinable()) {
            writerThread_.join();
        }
    }
    
    /**
     * Start the writer thread (called automatically on first use)
     */
    void start() {
        if (running_) return;
        
        running_ = true;
        writerThread_ = std::thread(&AsyncWriter::writerLoop, this);
    }

private:
    AsyncWriter() : running_(false), currentlyWriting_(false) {
        start();
    }
    
    // Non-copyable
    AsyncWriter(const AsyncWriter&) = delete;
    AsyncWriter& operator=(const AsyncWriter&) = delete;
    
    void writerLoop() {
        while (running_ || !tasks_.empty()) {
            WriteTask task;
            
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this] { 
                    return !tasks_.empty() || !running_; 
                });
                
                if (tasks_.empty()) {
                    continue;
                }
                
                task = std::move(tasks_.front());
                tasks_.pop();
                currentlyWriting_ = true;
            }
            
            // Perform the actual write outside the lock
            performWrite(task);
            
            {
                std::lock_guard<std::mutex> lock(mutex_);
                currentlyWriting_ = false;
            }
            flushCondition_.notify_all();
        }
    }
    
    void performWrite(const WriteTask& task) {
        FILE* file = fopen(task.filename.c_str(), task.binary ? "wb" : "w");
        if (file) {
            fwrite(task.data.data(), 1, task.data.size(), file);
            fclose(file);
        }
    }
    
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::condition_variable flushCondition_;
    std::queue<WriteTask> tasks_;
    std::thread writerThread_;
    std::atomic<bool> running_;
    std::atomic<bool> currentlyWriting_;
};

/**
 * Helper function for easy async writes
 */
inline void asyncWrite(const std::string& filename, const void* data, size_t size, bool binary = true) {
    AsyncWriter::getInstance().write(filename, data, size, binary);
}

/**
 * Wait for all async writes to complete
 */
inline void asyncFlush() {
    AsyncWriter::getInstance().flush();
}

} // namespace oc

#endif // ASYNC_WRITER_H
