# Application.mk - Optimized for ARM64 performance (Clang/LLVM)
# Target: Huawei P30 Pro (Kirin 980), Samsung S20 Ultra (Exynos 990 / SD865)
# NDK uses Clang - all flags must be Clang-compatible

APP_STL := c++_shared
APP_PLATFORM := android-24
APP_ABI := arm64-v8a

# C++17 with exceptions and RTTI (needed for OpenCV)
APP_CPPFLAGS := -std=c++17 -fexceptions -frtti

# Aggressive optimization flags (Clang-compatible)
# -O3: Maximum optimization level
# -ffast-math: Allow aggressive floating-point optimizations
# -fno-finite-math-only: Keep NaN/Inf handling (important for depth data)
APP_CPPFLAGS += -O3 -ffast-math -fno-finite-math-only

# ARM64 specific optimizations
# -march=armv8-a+simd: Enable ARMv8 with NEON SIMD
# -mtune=cortex-a76: Optimize for modern big cores
APP_CPPFLAGS += -march=armv8-a+simd -mtune=cortex-a76

# Code size and link-time optimization
# -ffunction-sections -fdata-sections: Enable dead code elimination
# -fvisibility=hidden: Hide symbols by default (smaller binary, faster load)
APP_CPPFLAGS += -ffunction-sections -fdata-sections
APP_CPPFLAGS += -fvisibility=hidden -fvisibility-inlines-hidden

# Loop optimizations (Clang-compatible)
# -funroll-loops: Unroll small loops for better pipelining
# -fvectorize: Enable loop vectorization (Clang's vectorizer)
# -fslp-vectorize: Enable SLP (Superword-Level Parallelism) vectorization
APP_CPPFLAGS += -funroll-loops -fvectorize -fslp-vectorize

# C flags (same optimizations for C code)
APP_CFLAGS := -O3 -ffast-math -fno-finite-math-only
APP_CFLAGS += -march=armv8-a+simd -mtune=cortex-a76
APP_CFLAGS += -ffunction-sections -fdata-sections
APP_CFLAGS += -funroll-loops -fvectorize -fslp-vectorize

# Linker optimization
# --gc-sections: Remove unused code sections (works with -ffunction-sections)
# -flto: Enable Link Time Optimization for better inlining and code size reduction
APP_LDFLAGS := -Wl,--gc-sections -flto -ffat-lto-objects
