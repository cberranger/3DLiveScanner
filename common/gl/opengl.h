#ifndef GL_OPENGL_H
#define GL_OPENGL_H

#ifdef ANDROID
// Use OpenGL ES 3.2 for modern features (VAO, UBO, compute shaders, 24-bit depth)
// Both Huawei P30 Pro and Samsung S20 Ultra fully support ES 3.2
#include <GLES3/gl32.h>
#include <GLES3/gl3ext.h>
// Keep ES2 extensions for any legacy functionality
#include <GLES2/gl2ext.h>
#else
#define GL_GLEXT_PROTOTYPES
#include <GL/gl.h>
#include <algorithm>
#include <stdio.h>
#include <vector>
#endif

#define GLM_ENABLE_EXPERIMENTAL
#define GLM_FORCE_RADIANS
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/rotate_vector.hpp>

#ifdef ANDROID
#include <android/log.h>
#include <stdlib.h>
#include <string>
#include <vector>
#define LOGI(...) \
  __android_log_print(ANDROID_LOG_INFO, "arcore_app", __VA_ARGS__)
#define LOGE(...) \
  __android_log_print(ANDROID_LOG_ERROR, "arcore_app", __VA_ARGS__)
#else
#define LOGI(...); \
  printf(__VA_ARGS__); printf("\n")
#define LOGE(...); \
  printf(__VA_ARGS__); printf("\n")
#endif

// OpenGL ES version detection helper
namespace oc {
namespace gl {
    // Returns true if OpenGL ES 3.2 is available
    // WARNING: Must be called AFTER OpenGL context is created!
    inline bool supportsES32() {
#ifdef ANDROID
        // Check if we have a valid GL context first
        // glGetString returns null if no context
        const GLubyte* version = glGetString(GL_VERSION);
        if (!version) {
            return false;  // No GL context yet
        }
        
        GLint major = 0, minor = 0;
        glGetIntegerv(GL_MAJOR_VERSION, &major);
        glGetIntegerv(GL_MINOR_VERSION, &minor);
        return (major > 3) || (major == 3 && minor >= 2);
#else
        return true;  // Desktop GL assumed to support equivalent features
#endif
    }
    
    // Returns true if compute shaders are available
    inline bool supportsComputeShaders() {
#ifdef ANDROID
        return supportsES32();
#else
        return true;
#endif
    }
}
}

#endif
