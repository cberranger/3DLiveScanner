#ifndef MEMORY_POOL_H
#define MEMORY_POOL_H

/**
 * Memory Pool for 3D Live Scanner
 * 
 * Provides pre-allocated memory pools to eliminate per-frame heap allocations.
 * Significantly reduces GC pressure and allocation overhead.
 * 
 * Includes specialized pools for:
 * - Image buffers (frame processing)
 * - Point cloud data (depth processing)
 * - Generic objects (small allocations)
 */

#include <vector>
#include <stack>
#include <mutex>
#include <memory>
#include <cstdint>
#include <cstring>
#include <cassert>

namespace oc {

/**
 * Fixed-size buffer pool for image data
 * Pre-allocates buffers at a fixed resolution to avoid per-frame allocations
 */
class ImageBufferPool {
public:
    struct Buffer {
        uint8_t* data;
        size_t size;
        int width;
        int height;
        int channels;
        bool inUse;
        
        Buffer() : data(nullptr), size(0), width(0), height(0), channels(4), inUse(false) {}
    };
    
    /**
     * Create pool with specified capacity
     * @param maxWidth Maximum image width
     * @param maxHeight Maximum image height
     * @param channels Bytes per pixel (default 4 for RGBA)
     * @param poolSize Number of buffers to pre-allocate
     */
    ImageBufferPool(int maxWidth = 1920, int maxHeight = 1080, 
                    int channels = 4, size_t poolSize = 4)
        : maxWidth_(maxWidth), maxHeight_(maxHeight), channels_(channels)
    {
        size_t bufferSize = maxWidth * maxHeight * channels;
        buffers_.resize(poolSize);
        
        for (size_t i = 0; i < poolSize; ++i) {
            buffers_[i].data = new uint8_t[bufferSize];
            buffers_[i].size = bufferSize;
            buffers_[i].width = maxWidth;
            buffers_[i].height = maxHeight;
            buffers_[i].channels = channels;
            buffers_[i].inUse = false;
            freeList_.push(i);
        }
    }
    
    ~ImageBufferPool() {
        for (auto& buffer : buffers_) {
            delete[] buffer.data;
        }
    }
    
    // Non-copyable
    ImageBufferPool(const ImageBufferPool&) = delete;
    ImageBufferPool& operator=(const ImageBufferPool&) = delete;
    
    /**
     * Acquire a buffer from the pool
     * @param width Requested width (must be <= maxWidth)
     * @param height Requested height (must be <= maxHeight)
     * @return Pointer to buffer, or nullptr if pool exhausted
     */
    Buffer* acquire(int width, int height) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (freeList_.empty()) {
            // Pool exhausted - could expand here if needed
            return nullptr;
        }
        
        assert(width <= maxWidth_ && height <= maxHeight_);
        
        size_t idx = freeList_.top();
        freeList_.pop();
        
        Buffer* buffer = &buffers_[idx];
        buffer->width = width;
        buffer->height = height;
        buffer->inUse = true;
        
        return buffer;
    }
    
    /**
     * Release a buffer back to the pool
     */
    void release(Buffer* buffer) {
        if (!buffer) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Find index of this buffer
        for (size_t i = 0; i < buffers_.size(); ++i) {
            if (&buffers_[i] == buffer) {
                buffer->inUse = false;
                freeList_.push(i);
                return;
            }
        }
    }
    
    /**
     * Get number of available buffers
     */
    size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return freeList_.size();
    }
    
    /**
     * Get total pool size
     */
    size_t capacity() const {
        return buffers_.size();
    }

private:
    std::vector<Buffer> buffers_;
    std::stack<size_t> freeList_;
    mutable std::mutex mutex_;
    int maxWidth_;
    int maxHeight_;
    int channels_;
};

/**
 * Point cloud buffer pool for depth data
 * Pre-allocates buffers for 3D point storage
 */
class PointCloudPool {
public:
    struct PointBuffer {
        float* data;       // x,y,z,confidence interleaved
        size_t capacity;   // Max points
        size_t count;      // Current point count
        bool inUse;
        
        PointBuffer() : data(nullptr), capacity(0), count(0), inUse(false) {}
        
        // Convenience accessors
        float* pointAt(size_t idx) { return data + idx * 4; }
        const float* pointAt(size_t idx) const { return data + idx * 4; }
    };
    
    /**
     * Create pool with specified capacity
     * @param maxPoints Maximum points per buffer
     * @param poolSize Number of buffers to pre-allocate
     */
    PointCloudPool(size_t maxPoints = 300000, size_t poolSize = 3)
        : maxPoints_(maxPoints)
    {
        size_t bufferSize = maxPoints * 4;  // 4 floats per point (x,y,z,conf)
        buffers_.resize(poolSize);
        
        for (size_t i = 0; i < poolSize; ++i) {
            buffers_[i].data = new float[bufferSize];
            buffers_[i].capacity = maxPoints;
            buffers_[i].count = 0;
            buffers_[i].inUse = false;
            freeList_.push(i);
        }
    }
    
    ~PointCloudPool() {
        for (auto& buffer : buffers_) {
            delete[] buffer.data;
        }
    }
    
    // Non-copyable
    PointCloudPool(const PointCloudPool&) = delete;
    PointCloudPool& operator=(const PointCloudPool&) = delete;
    
    /**
     * Acquire a buffer from the pool
     * @return Pointer to buffer, or nullptr if pool exhausted
     */
    PointBuffer* acquire() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (freeList_.empty()) {
            return nullptr;
        }
        
        size_t idx = freeList_.top();
        freeList_.pop();
        
        PointBuffer* buffer = &buffers_[idx];
        buffer->count = 0;
        buffer->inUse = true;
        
        return buffer;
    }
    
    /**
     * Release a buffer back to the pool
     */
    void release(PointBuffer* buffer) {
        if (!buffer) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        for (size_t i = 0; i < buffers_.size(); ++i) {
            if (&buffers_[i] == buffer) {
                buffer->inUse = false;
                buffer->count = 0;
                freeList_.push(i);
                return;
            }
        }
    }
    
    size_t available() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return freeList_.size();
    }
    
    size_t capacity() const {
        return buffers_.size();
    }

private:
    std::vector<PointBuffer> buffers_;
    std::stack<size_t> freeList_;
    mutable std::mutex mutex_;
    size_t maxPoints_;
};

/**
 * Generic object pool using placement new
 * For small, frequently allocated objects
 */
template<typename T, size_t BlockSize = 64>
class ObjectPool {
public:
    ObjectPool() : freeCount_(0) {
        // Pre-allocate first block
        allocateBlock();
    }
    
    ~ObjectPool() {
        for (auto* block : blocks_) {
            ::operator delete(block);
        }
    }
    
    // Non-copyable
    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;
    
    /**
     * Allocate an object from the pool
     */
    template<typename... Args>
    T* allocate(Args&&... args) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (freeList_.empty()) {
            allocateBlock();
        }
        
        T* ptr = freeList_.top();
        freeList_.pop();
        --freeCount_;
        
        // Construct in place
        new (ptr) T(std::forward<Args>(args)...);
        return ptr;
    }
    
    /**
     * Return an object to the pool
     */
    void deallocate(T* ptr) {
        if (!ptr) return;
        
        // Call destructor
        ptr->~T();
        
        std::lock_guard<std::mutex> lock(mutex_);
        freeList_.push(ptr);
        ++freeCount_;
    }
    
    size_t freeCount() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return freeCount_;
    }

private:
    void allocateBlock() {
        // Allocate raw memory for BlockSize objects
        size_t blockBytes = BlockSize * sizeof(T);
        char* block = static_cast<char*>(::operator new(blockBytes));
        blocks_.push_back(block);
        
        // Add all slots to free list
        for (size_t i = 0; i < BlockSize; ++i) {
            T* slot = reinterpret_cast<T*>(block + i * sizeof(T));
            freeList_.push(slot);
            ++freeCount_;
        }
    }
    
    std::vector<char*> blocks_;
    std::stack<T*> freeList_;
    mutable std::mutex mutex_;
    size_t freeCount_;
};

/**
 * RAII wrapper for pooled buffers
 * Automatically releases buffer when scope exits
 */
template<typename PoolType>
class PooledBuffer {
public:
    using BufferType = decltype(std::declval<PoolType>().acquire());
    
    PooledBuffer(PoolType& pool) : pool_(pool), buffer_(pool.acquire()) {}
    
    ~PooledBuffer() {
        if (buffer_) {
            pool_.release(buffer_);
        }
    }
    
    // Move only
    PooledBuffer(PooledBuffer&& other) noexcept 
        : pool_(other.pool_), buffer_(other.buffer_) {
        other.buffer_ = nullptr;
    }
    
    PooledBuffer& operator=(PooledBuffer&& other) noexcept {
        if (this != &other) {
            if (buffer_) pool_.release(buffer_);
            buffer_ = other.buffer_;
            other.buffer_ = nullptr;
        }
        return *this;
    }
    
    // Non-copyable
    PooledBuffer(const PooledBuffer&) = delete;
    PooledBuffer& operator=(const PooledBuffer&) = delete;
    
    BufferType get() { return buffer_; }
    BufferType operator->() { return buffer_; }
    explicit operator bool() const { return buffer_ != nullptr; }

private:
    PoolType& pool_;
    BufferType buffer_;
};

/**
 * Global memory pools singleton
 * Provides application-wide pools for common allocations
 */
class GlobalPools {
public:
    static GlobalPools& instance() {
        static GlobalPools pools;
        return pools;
    }
    
    ImageBufferPool& imagePool() { return imagePool_; }
    PointCloudPool& pointCloudPool() { return pointCloudPool_; }
    
    // Convenience methods
    static ImageBufferPool::Buffer* acquireImage(int w, int h) {
        return instance().imagePool_.acquire(w, h);
    }
    
    static void releaseImage(ImageBufferPool::Buffer* buf) {
        instance().imagePool_.release(buf);
    }
    
    static PointCloudPool::PointBuffer* acquirePointCloud() {
        return instance().pointCloudPool_.acquire();
    }
    
    static void releasePointCloud(PointCloudPool::PointBuffer* buf) {
        instance().pointCloudPool_.release(buf);
    }
    
    /**
     * Get pool statistics for debugging
     */
    struct Stats {
        size_t imagePoolAvailable;
        size_t imagePoolCapacity;
        size_t pointCloudPoolAvailable;
        size_t pointCloudPoolCapacity;
    };
    
    Stats getStats() const {
        return {
            imagePool_.available(),
            imagePool_.capacity(),
            pointCloudPool_.available(),
            pointCloudPool_.capacity()
        };
    }

private:
    GlobalPools() 
        : imagePool_(1920, 1080, 4, 4)      // 4 RGBA buffers at 1080p
        , pointCloudPool_(300000, 3)         // 3 buffers of 300k points each
    {}
    
    ImageBufferPool imagePool_;
    PointCloudPool pointCloudPool_;
};

} // namespace oc

#endif // MEMORY_POOL_H
