#ifndef GL_COMPUTE_SHADER_H
#define GL_COMPUTE_SHADER_H

/**
 * OpenGL ES 3.2 Compute Shader Wrapper
 * 
 * Provides GPU-accelerated compute operations for depth processing.
 * Requires OpenGL ES 3.2 (available on P30 Pro and S20 Ultra).
 * 
 * Features:
 * - Depth buffer operations (fill, masking)
 * - Image processing (edge detection, filtering)
 * - Automatic CPU fallback
 */

#include "gl/opengl.h"
#include <string>
#include <vector>

namespace oc {
namespace gl {

/**
 * Compute shader wrapper for GPU operations
 */
class ComputeShader {
public:
    ComputeShader() : programId_(0), shaderId_(0), valid_(false) {}
    
    ~ComputeShader() {
        destroy();
    }
    
    // Non-copyable
    ComputeShader(const ComputeShader&) = delete;
    ComputeShader& operator=(const ComputeShader&) = delete;
    
    /**
     * Check if compute shaders are supported
     */
    static bool isSupported() {
#ifdef ANDROID
        return supportsComputeShaders();
#else
        return true;
#endif
    }
    
    /**
     * Compile compute shader from source
     * @param source GLSL compute shader source code
     * @return true if compilation succeeded
     */
    bool compile(const std::string& source) {
        if (!isSupported()) {
            LOGE("Compute shaders not supported");
            return false;
        }
        
        destroy();
        
#ifdef ANDROID
        // Add version header
        std::string fullSource = "#version 320 es\n" + source;
        
        // Create and compile shader
        shaderId_ = glCreateShader(GL_COMPUTE_SHADER);
        const char* src = fullSource.c_str();
        glShaderSource(shaderId_, 1, &src, nullptr);
        glCompileShader(shaderId_);
        
        // Check compilation
        GLint success;
        glGetShaderiv(shaderId_, GL_COMPILE_STATUS, &success);
        if (!success) {
            char log[512];
            glGetShaderInfoLog(shaderId_, 512, nullptr, log);
            LOGE("Compute shader compile error: %s", log);
            glDeleteShader(shaderId_);
            shaderId_ = 0;
            return false;
        }
        
        // Create program and link
        programId_ = glCreateProgram();
        glAttachShader(programId_, shaderId_);
        glLinkProgram(programId_);
        
        // Check linking
        glGetProgramiv(programId_, GL_LINK_STATUS, &success);
        if (!success) {
            char log[512];
            glGetProgramInfoLog(programId_, 512, nullptr, log);
            LOGE("Compute program link error: %s", log);
            glDeleteProgram(programId_);
            glDeleteShader(shaderId_);
            programId_ = 0;
            shaderId_ = 0;
            return false;
        }
        
        valid_ = true;
        return true;
#else
        return false;
#endif
    }
    
    /**
     * Bind this compute shader for use
     */
    void bind() {
        if (valid_) {
            glUseProgram(programId_);
        }
    }
    
    /**
     * Unbind compute shader
     */
    void unbind() {
        glUseProgram(0);
    }
    
    /**
     * Dispatch compute shader
     * @param numGroupsX Number of work groups in X
     * @param numGroupsY Number of work groups in Y
     * @param numGroupsZ Number of work groups in Z
     */
    void dispatch(GLuint numGroupsX, GLuint numGroupsY, GLuint numGroupsZ) {
#ifdef ANDROID
        if (valid_) {
            glDispatchCompute(numGroupsX, numGroupsY, numGroupsZ);
        }
#endif
    }
    
    /**
     * Wait for compute shader to complete
     */
    void barrier() {
#ifdef ANDROID
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);
#endif
    }
    
    /**
     * Set uniform int value
     */
    void setUniform(const char* name, int value) {
        if (valid_) {
            GLint loc = glGetUniformLocation(programId_, name);
            if (loc >= 0) glUniform1i(loc, value);
        }
    }
    
    /**
     * Set uniform float value
     */
    void setUniform(const char* name, float value) {
        if (valid_) {
            GLint loc = glGetUniformLocation(programId_, name);
            if (loc >= 0) glUniform1f(loc, value);
        }
    }
    
    /**
     * Set uniform vec2 value
     */
    void setUniform(const char* name, float x, float y) {
        if (valid_) {
            GLint loc = glGetUniformLocation(programId_, name);
            if (loc >= 0) glUniform2f(loc, x, y);
        }
    }
    
    /**
     * Set uniform ivec2 value (for image sizes)
     */
    void setUniform(const char* name, int x, int y) {
        if (valid_) {
            GLint loc = glGetUniformLocation(programId_, name);
            if (loc >= 0) glUniform2i(loc, x, y);
        }
    }
    
    /**
     * Set uniform vec4 value
     */
    void setUniform(const char* name, float x, float y, float z, float w) {
        if (valid_) {
            GLint loc = glGetUniformLocation(programId_, name);
            if (loc >= 0) glUniform4f(loc, x, y, z, w);
        }
    }
    
    /**
     * Bind image texture for compute shader access
     */
    void bindImage(GLuint unit, GLuint texture, GLenum access, GLenum format) {
#ifdef ANDROID
        if (valid_) {
            glBindImageTexture(unit, texture, 0, GL_FALSE, 0, access, format);
        }
#endif
    }
    
    /**
     * Check if shader is valid and ready to use
     */
    bool isValid() const { return valid_; }
    
    /**
     * Get program ID
     */
    GLuint getProgramId() const { return programId_; }
    
    /**
     * Destroy shader resources
     */
    void destroy() {
        if (programId_) {
            glDeleteProgram(programId_);
            programId_ = 0;
        }
        if (shaderId_) {
            glDeleteShader(shaderId_);
            shaderId_ = 0;
        }
        valid_ = false;
    }

private:
    GLuint programId_;
    GLuint shaderId_;
    bool valid_;
};

/**
 * Pre-built compute shaders for common depth operations
 */
namespace DepthShaders {

/**
 * Get depth buffer fill shader source
 * Fills holes in depth buffer using neighboring valid pixels
 */
inline std::string getDepthFillShader() {
    return R"(
layout(local_size_x = 16, local_size_y = 16) in;

layout(r32f, binding = 0) readonly uniform highp image2D inputDepth;
layout(r32f, binding = 1) writeonly uniform highp image2D outputDepth;

uniform ivec2 imageSize;
uniform float maxDepthDelta;

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= imageSize.x || pos.y >= imageSize.y) return;
    
    float center = imageLoad(inputDepth, pos).r;
    
    // If valid, just copy
    if (center > 0.0) {
        imageStore(outputDepth, pos, vec4(center));
        return;
    }
    
    // Sample 8-neighborhood
    float sum = 0.0;
    float count = 0.0;
    
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            
            ivec2 np = pos + ivec2(dx, dy);
            if (np.x < 0 || np.x >= imageSize.x || np.y < 0 || np.y >= imageSize.y) continue;
            
            float d = imageLoad(inputDepth, np).r;
            if (d > 0.0) {
                sum += d;
                count += 1.0;
            }
        }
    }
    
    if (count > 0.0) {
        imageStore(outputDepth, pos, vec4(sum / count));
    } else {
        imageStore(outputDepth, pos, vec4(0.0));
    }
}
)";
}

/**
 * Get edge mask shader source
 * Creates a mask for depth discontinuities (edges)
 */
inline std::string getEdgeMaskShader() {
    return R"(
layout(local_size_x = 16, local_size_y = 16) in;

layout(r32f, binding = 0) readonly uniform highp image2D inputDepth;
layout(r8, binding = 1) writeonly uniform highp image2D outputMask;

uniform ivec2 imageSize;
uniform float edgeThreshold;

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= imageSize.x || pos.y >= imageSize.y) return;
    
    // Border pixels are edges
    if (pos.x == 0 || pos.x == imageSize.x - 1 || 
        pos.y == 0 || pos.y == imageSize.y - 1) {
        imageStore(outputMask, pos, vec4(1.0));
        return;
    }
    
    float center = imageLoad(inputDepth, pos).r;
    if (center <= 0.0) {
        imageStore(outputMask, pos, vec4(0.0));
        return;
    }
    
    float left = imageLoad(inputDepth, pos + ivec2(-1, 0)).r;
    float right = imageLoad(inputDepth, pos + ivec2(1, 0)).r;
    float up = imageLoad(inputDepth, pos + ivec2(0, -1)).r;
    float down = imageLoad(inputDepth, pos + ivec2(0, 1)).r;
    
    float dx = (left > 0.0 && right > 0.0) ? abs(right - left) : 0.0;
    float dy = (up > 0.0 && down > 0.0) ? abs(up - down) : 0.0;
    
    float isEdge = (max(dx, dy) > edgeThreshold) ? 1.0 : 0.0;
    imageStore(outputMask, pos, vec4(isEdge));
}
)";
}

/**
 * Get bilateral filter shader source
 * Edge-preserving smoothing of depth
 */
inline std::string getBilateralFilterShader() {
    return R"(
layout(local_size_x = 16, local_size_y = 16) in;

layout(r32f, binding = 0) readonly uniform highp image2D inputDepth;
layout(r32f, binding = 1) writeonly uniform highp image2D outputDepth;

uniform ivec2 imageSize;
uniform float spatialSigma;
uniform float depthSigma;
uniform int kernelRadius;

void main() {
    ivec2 pos = ivec2(gl_GlobalInvocationID.xy);
    if (pos.x >= imageSize.x || pos.y >= imageSize.y) return;
    
    float centerDepth = imageLoad(inputDepth, pos).r;
    if (centerDepth <= 0.0) {
        imageStore(outputDepth, pos, vec4(0.0));
        return;
    }
    
    float sum = 0.0;
    float wSum = 0.0;
    
    float spatialSigma2 = 2.0 * spatialSigma * spatialSigma;
    float depthSigma2 = 2.0 * depthSigma * depthSigma;
    
    for (int dy = -kernelRadius; dy <= kernelRadius; dy++) {
        for (int dx = -kernelRadius; dx <= kernelRadius; dx++) {
            ivec2 np = pos + ivec2(dx, dy);
            if (np.x < 0 || np.x >= imageSize.x || np.y < 0 || np.y >= imageSize.y) continue;
            
            float d = imageLoad(inputDepth, np).r;
            if (d <= 0.0) continue;
            
            float spatialDist = float(dx * dx + dy * dy);
            float depthDist = (d - centerDepth) * (d - centerDepth);
            
            float w = exp(-spatialDist / spatialSigma2) * exp(-depthDist / depthSigma2);
            sum += d * w;
            wSum += w;
        }
    }
    
    if (wSum > 0.0) {
        imageStore(outputDepth, pos, vec4(sum / wSum));
    } else {
        imageStore(outputDepth, pos, vec4(centerDepth));
    }
}
)";
}

} // namespace DepthShaders

/**
 * GPU Depth Processor using compute shaders
 */
class GPUDepthProcessor {
public:
    GPUDepthProcessor() : initialized_(false) {}
    
    /**
     * Initialize GPU depth processor
     * @return true if GPU acceleration is available
     */
    bool initialize() {
        if (!ComputeShader::isSupported()) {
            LOGI("Compute shaders not supported, using CPU fallback");
            return false;
        }
        
        // Compile shaders
        if (!depthFillShader_.compile(DepthShaders::getDepthFillShader())) {
            LOGE("Failed to compile depth fill shader");
            return false;
        }
        
        if (!edgeMaskShader_.compile(DepthShaders::getEdgeMaskShader())) {
            LOGE("Failed to compile edge mask shader");
            return false;
        }
        
        if (!bilateralShader_.compile(DepthShaders::getBilateralFilterShader())) {
            LOGE("Failed to compile bilateral filter shader");
            return false;
        }
        
        initialized_ = true;
        LOGI("GPU depth processor initialized");
        return true;
    }
    
    /**
     * Check if GPU processing is available
     */
    bool isAvailable() const { return initialized_; }
    
    /**
     * Fill holes in depth buffer using GPU
     */
    void fillHoles(GLuint inputTex, GLuint outputTex, int width, int height, 
                   float maxDelta = 0.05f) {
        if (!initialized_) return;
        
        depthFillShader_.bind();
        depthFillShader_.setUniform("imageSize", width, height);
        depthFillShader_.setUniform("maxDepthDelta", maxDelta);
        depthFillShader_.bindImage(0, inputTex, GL_READ_ONLY, GL_R32F);
        depthFillShader_.bindImage(1, outputTex, GL_WRITE_ONLY, GL_R32F);
        
        GLuint groupsX = (width + 15) / 16;
        GLuint groupsY = (height + 15) / 16;
        depthFillShader_.dispatch(groupsX, groupsY, 1);
        depthFillShader_.barrier();
        depthFillShader_.unbind();
    }
    
    /**
     * Create edge mask using GPU
     */
    void createEdgeMask(GLuint depthTex, GLuint maskTex, int width, int height,
                        float threshold = 0.02f) {
        if (!initialized_) return;
        
        edgeMaskShader_.bind();
        edgeMaskShader_.setUniform("imageSize", width, height);
        edgeMaskShader_.setUniform("edgeThreshold", threshold);
        edgeMaskShader_.bindImage(0, depthTex, GL_READ_ONLY, GL_R32F);
        edgeMaskShader_.bindImage(1, maskTex, GL_WRITE_ONLY, GL_R8);
        
        GLuint groupsX = (width + 15) / 16;
        GLuint groupsY = (height + 15) / 16;
        edgeMaskShader_.dispatch(groupsX, groupsY, 1);
        edgeMaskShader_.barrier();
        edgeMaskShader_.unbind();
    }
    
    /**
     * Apply bilateral filter using GPU
     */
    void bilateralFilter(GLuint inputTex, GLuint outputTex, int width, int height,
                         float spatialSigma = 2.0f, float depthSigma = 0.01f, 
                         int radius = 3) {
        if (!initialized_) return;
        
        bilateralShader_.bind();
        bilateralShader_.setUniform("imageSize", width, height);
        bilateralShader_.setUniform("spatialSigma", spatialSigma);
        bilateralShader_.setUniform("depthSigma", depthSigma);
        bilateralShader_.setUniform("kernelRadius", radius);
        bilateralShader_.bindImage(0, inputTex, GL_READ_ONLY, GL_R32F);
        bilateralShader_.bindImage(1, outputTex, GL_WRITE_ONLY, GL_R32F);
        
        GLuint groupsX = (width + 15) / 16;
        GLuint groupsY = (height + 15) / 16;
        bilateralShader_.dispatch(groupsX, groupsY, 1);
        bilateralShader_.barrier();
        bilateralShader_.unbind();
    }

private:
    bool initialized_;
    ComputeShader depthFillShader_;
    ComputeShader edgeMaskShader_;
    ComputeShader bilateralShader_;
};

} // namespace gl
} // namespace oc

#endif // GL_COMPUTE_SHADER_H
