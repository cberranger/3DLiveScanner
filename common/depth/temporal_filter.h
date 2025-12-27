#ifndef DEPTH_TEMPORAL_FILTER_H
#define DEPTH_TEMPORAL_FILTER_H

/**
 * Temporal Depth Filter for ToF Sensors
 * 
 * Reduces noise in ToF depth data by integrating multiple frames over time.
 * Uses exponential moving average with confidence weighting.
 * 
 * Features:
 * - 3-frame temporal history
 * - Confidence-weighted integration
 * - Flying pixel rejection
 * - Edge-preserving filtering
 * 
 * Designed for: Huawei P30 Pro ToF, Samsung S20 Ultra ToF
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace oc {
namespace depth {

/**
 * Configuration for temporal filter
 */
struct TemporalFilterConfig {
    // Number of frames to keep in history
    static constexpr int HISTORY_SIZE = 3;
    
    // Exponential moving average alpha (0.0-1.0)
    // Higher = more weight on current frame, lower = more smoothing
    float alpha = 0.5f;
    
    // Maximum depth change between frames to consider valid (meters)
    // Larger changes are considered flying pixels and rejected
    float maxDepthDelta = 0.05f;
    
    // Minimum confidence to include a pixel (0.0-1.0)
    float minConfidence = 0.1f;
    
    // Enable edge-preserving mode
    bool preserveEdges = true;
    
    // Edge threshold (depth gradient)
    float edgeThreshold = 0.02f;
};

/**
 * Single depth frame with metadata
 */
struct DepthFrame {
    float* depth;           // Depth values in meters (nullptr if invalid)
    float* confidence;      // Per-pixel confidence (0.0-1.0)
    int width;
    int height;
    double timestamp;       // Frame timestamp in seconds
    bool valid;
    
    DepthFrame() : depth(nullptr), confidence(nullptr), width(0), height(0), 
                   timestamp(0), valid(false) {}
    
    ~DepthFrame() {
        release();
    }
    
    // No copying
    DepthFrame(const DepthFrame&) = delete;
    DepthFrame& operator=(const DepthFrame&) = delete;
    
    // Move semantics
    DepthFrame(DepthFrame&& other) noexcept {
        depth = other.depth;
        confidence = other.confidence;
        width = other.width;
        height = other.height;
        timestamp = other.timestamp;
        valid = other.valid;
        other.depth = nullptr;
        other.confidence = nullptr;
        other.valid = false;
    }
    
    DepthFrame& operator=(DepthFrame&& other) noexcept {
        if (this != &other) {
            release();
            depth = other.depth;
            confidence = other.confidence;
            width = other.width;
            height = other.height;
            timestamp = other.timestamp;
            valid = other.valid;
            other.depth = nullptr;
            other.confidence = nullptr;
            other.valid = false;
        }
        return *this;
    }
    
    void allocate(int w, int h) {
        release();
        width = w;
        height = h;
        int size = w * h;
        depth = new float[size];
        confidence = new float[size];
        valid = true;
    }
    
    void release() {
        if (depth) {
            delete[] depth;
            depth = nullptr;
        }
        if (confidence) {
            delete[] confidence;
            confidence = nullptr;
        }
        valid = false;
    }
    
    int size() const { return width * height; }
};

/**
 * Temporal depth filter with frame history
 */
class TemporalDepthFilter {
public:
    TemporalDepthFilter() : historyIndex_(0), frameCount_(0) {
        config_ = TemporalFilterConfig();
    }
    
    explicit TemporalDepthFilter(const TemporalFilterConfig& config) 
        : config_(config), historyIndex_(0), frameCount_(0) {}
    
    /**
     * Set filter configuration
     */
    void setConfig(const TemporalFilterConfig& config) {
        config_ = config;
    }
    
    /**
     * Get current configuration
     */
    const TemporalFilterConfig& getConfig() const {
        return config_;
    }
    
    /**
     * Add a new depth frame to the history
     */
    void addFrame(const float* depthData, const float* confidenceData,
                  int width, int height, double timestamp) {
        DepthFrame& frame = history_[historyIndex_];
        if (!frame.valid || frame.width != width || frame.height != height) {
            frame.allocate(width, height);
        }
        
        int size = width * height;
        std::memcpy(frame.depth, depthData, size * sizeof(float));
        
        if (confidenceData) {
            std::memcpy(frame.confidence, confidenceData, size * sizeof(float));
        } else {
            std::fill(frame.confidence, frame.confidence + size, 1.0f);
        }
        
        frame.timestamp = timestamp;
        frame.valid = true;
        
        historyIndex_ = (historyIndex_ + 1) % TemporalFilterConfig::HISTORY_SIZE;
        frameCount_ = std::min(frameCount_ + 1, TemporalFilterConfig::HISTORY_SIZE);
    }
    
    /**
     * Get filtered depth output
     */
    bool getFiltered(float* outputDepth, float* outputConfidence, 
                     int width, int height) {
        if (frameCount_ < 1) return false;
        
        int latestIdx = (historyIndex_ + TemporalFilterConfig::HISTORY_SIZE - 1) 
                        % TemporalFilterConfig::HISTORY_SIZE;
        const DepthFrame& latest = history_[latestIdx];
        
        if (!latest.valid || latest.width != width || latest.height != height) {
            return false;
        }
        
        int size = width * height;
        
        if (frameCount_ == 1) {
            std::memcpy(outputDepth, latest.depth, size * sizeof(float));
            if (outputConfidence) {
                std::memcpy(outputConfidence, latest.confidence, size * sizeof(float));
            }
            return true;
        }
        
        for (int i = 0; i < size; ++i) {
            float currentDepth = latest.depth[i];
            float currentConf = latest.confidence[i];
            
            if (currentDepth <= 0 || currentConf < config_.minConfidence) {
                outputDepth[i] = currentDepth;
                if (outputConfidence) outputConfidence[i] = currentConf;
                continue;
            }
            
            bool isEdge = false;
            if (config_.preserveEdges) {
                isEdge = isEdgePixel(latest.depth, width, height, i);
            }
            
            float alpha = isEdge ? 0.8f : config_.alpha;
            float weightedSum = currentDepth * currentConf * alpha;
            float totalWeight = currentConf * alpha;
            
            for (int f = 1; f < frameCount_; ++f) {
                int fIdx = (latestIdx + TemporalFilterConfig::HISTORY_SIZE - f) 
                           % TemporalFilterConfig::HISTORY_SIZE;
                const DepthFrame& prevFrame = history_[fIdx];
                
                if (!prevFrame.valid) continue;
                
                float prevDepth = prevFrame.depth[i];
                float prevConf = prevFrame.confidence[i];
                
                if (prevDepth <= 0 || prevConf < config_.minConfidence) continue;
                
                float depthDelta = std::abs(currentDepth - prevDepth);
                if (depthDelta > config_.maxDepthDelta) continue;
                
                float frameWeight = std::pow(1.0f - alpha, static_cast<float>(f));
                float weight = prevConf * frameWeight;
                
                weightedSum += prevDepth * weight;
                totalWeight += weight;
            }
            
            if (totalWeight > 0) {
                outputDepth[i] = weightedSum / totalWeight;
                if (outputConfidence) outputConfidence[i] = std::min(totalWeight, 1.0f);
            } else {
                outputDepth[i] = currentDepth;
                if (outputConfidence) outputConfidence[i] = currentConf;
            }
        }
        
        return true;
    }
    
    void reset() {
        for (int i = 0; i < TemporalFilterConfig::HISTORY_SIZE; ++i) {
            history_[i].release();
        }
        historyIndex_ = 0;
        frameCount_ = 0;
    }
    
    int getFrameCount() const { return frameCount_; }

private:
    bool isEdgePixel(const float* depth, int width, int height, int idx) const {
        int x = idx % width;
        int y = idx / width;
        
        if (x == 0 || x == width - 1 || y == 0 || y == height - 1) return true;
        
        float center = depth[idx];
        if (center <= 0) return false;
        
        float left = depth[idx - 1];
        float right = depth[idx + 1];
        float up = depth[idx - width];
        float down = depth[idx + width];
        
        float dx = (right > 0 && left > 0) ? std::abs(right - left) : 0;
        float dy = (up > 0 && down > 0) ? std::abs(up - down) : 0;
        
        return std::max(dx, dy) > config_.edgeThreshold;
    }
    
    TemporalFilterConfig config_;
    std::array<DepthFrame, TemporalFilterConfig::HISTORY_SIZE> history_;
    int historyIndex_;
    int frameCount_;
};

} // namespace depth
} // namespace oc

#endif // DEPTH_TEMPORAL_FILTER_H
