package com.lvonasek.gles;

import android.opengl.EGL14;
import android.opengl.EGLExt;

import javax.microedition.khronos.egl.EGL10;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.egl.EGLDisplay;

/**
 * EGL Configuration Chooser optimized for OpenGL ES 3.2
 * 
 * Requests ES 3.2 context for modern features:
 * - Vertex Array Objects (VAO)
 * - Uniform Buffer Objects (UBO)
 * - Compute Shaders
 * - 24-bit depth buffer
 * 
 * Falls back to ES 2.0 if 3.2 is not available (unlikely on P30 Pro / S20 Ultra)
 */
class EGLConfigChooser
{
  // EGL constants for OpenGL ES 3.x
  private static final int EGL_OPENGL_ES2_BIT = 4;
  private static final int EGL_OPENGL_ES3_BIT = 64;  // 0x40
  
  private int[] mConfigSpec;
  private int[] mValue;
  // Subclasses can adjust these values:
  private int mRedSize;
  private int mGreenSize;
  private int mBlueSize;
  private int mAlphaSize;
  private int mDepthSize;
  private int mStencilSize;
  private boolean mUseES3;

  EGLConfigChooser(int redSize, int greenSize, int blueSize, int alphaSize, int depthSize, int stencilSize)
  {
    // Request 24-bit depth by default for better precision
    if (depthSize < 24) {
      depthSize = 24;
    }
    
    mValue = new int[1];
    mRedSize = redSize;
    mGreenSize = greenSize;
    mBlueSize = blueSize;
    mAlphaSize = alphaSize;
    mDepthSize = depthSize;
    mStencilSize = stencilSize;
    
    // Try ES 3.2 first
    mUseES3 = true;
    mConfigSpec = filterConfigSpec(new int[] { 
            EGL10.EGL_RED_SIZE, redSize, 
            EGL10.EGL_GREEN_SIZE, greenSize, 
            EGL10.EGL_BLUE_SIZE, blueSize,
            EGL10.EGL_ALPHA_SIZE, alphaSize, 
            EGL10.EGL_DEPTH_SIZE, depthSize, 
            EGL10.EGL_STENCIL_SIZE, stencilSize,
            EGL10.EGL_NONE });
  }

  EGLConfig chooseConfig(EGL10 egl, EGLDisplay display)
  {
    int[] num_config = new int[1];
    
    // First try with ES 3.x config
    if (!egl.eglChooseConfig(display, mConfigSpec, null, 0, num_config) || num_config[0] <= 0) {
      // Fallback to ES 2.0
      mUseES3 = false;
      mConfigSpec = filterConfigSpec(new int[] { 
              EGL10.EGL_RED_SIZE, mRedSize, 
              EGL10.EGL_GREEN_SIZE, mGreenSize, 
              EGL10.EGL_BLUE_SIZE, mBlueSize,
              EGL10.EGL_ALPHA_SIZE, mAlphaSize, 
              EGL10.EGL_DEPTH_SIZE, 16,  // Fallback to 16-bit depth 
              EGL10.EGL_STENCIL_SIZE, mStencilSize,
              EGL10.EGL_NONE });
      
      if (!egl.eglChooseConfig(display, mConfigSpec, null, 0, num_config)) {
        throw new IllegalArgumentException("eglChooseConfig failed");
      }
    }

    int numConfigs = num_config[0];
    if (numConfigs <= 0)
      throw new IllegalArgumentException("No configs match configSpec");

    EGLConfig[] configs = new EGLConfig[numConfigs];
    if (!egl.eglChooseConfig(display, mConfigSpec, configs, numConfigs, num_config))
      throw new IllegalArgumentException("eglChooseConfig#2 failed");
    EGLConfig config = chooseConfig(egl, display, configs);
    if (config == null)
      throw new IllegalArgumentException("No config chosen");
    return config;
  }
  
  /**
   * Check if ES 3.x is being used
   */
  public boolean isUsingES3() {
    return mUseES3;
  }
  
  /**
   * Get the EGL context client version to use
   * @return 3 for ES 3.2, 2 for ES 2.0
   */
  public int getContextClientVersion() {
    return mUseES3 ? 3 : 2;
  }

  private EGLConfig chooseConfig(EGL10 egl, EGLDisplay display, EGLConfig[] configs)
  {
    EGLConfig bestConfig = null;
    int bestDepth = 0;
    
    for (EGLConfig config : configs)
    {
      int d = findConfigAttribute(egl, display, config, EGL10.EGL_DEPTH_SIZE, 0);
      int s = findConfigAttribute(egl, display, config, EGL10.EGL_STENCIL_SIZE, 0);
      
      // Prefer configs with larger depth buffer
      if ((d >= mDepthSize || d >= 16) && (s >= mStencilSize))
      {
        int r = findConfigAttribute(egl, display, config, EGL10.EGL_RED_SIZE, 0);
        int g = findConfigAttribute(egl, display, config, EGL10.EGL_GREEN_SIZE, 0);
        int b = findConfigAttribute(egl, display, config, EGL10.EGL_BLUE_SIZE, 0);
        int a = findConfigAttribute(egl, display, config, EGL10.EGL_ALPHA_SIZE, 0);
        if ((r == mRedSize) && (g == mGreenSize) && (b == mBlueSize) && (a == mAlphaSize)) {
          // Prefer 24-bit depth over 16-bit
          if (d > bestDepth) {
            bestDepth = d;
            bestConfig = config;
          } else if (bestConfig == null) {
            bestConfig = config;
            bestDepth = d;
          }
        }
      }
    }
    return bestConfig;
  }

  private int[] filterConfigSpec(int[] configSpec)
  {
    int len = configSpec.length;
    int[] newConfigSpec = new int[len + 2];
    System.arraycopy(configSpec, 0, newConfigSpec, 0, len - 1);
    newConfigSpec[len - 1] = EGL10.EGL_RENDERABLE_TYPE;
    // Use ES 3.x bit if available, otherwise ES 2.x
    newConfigSpec[len] = mUseES3 ? EGL_OPENGL_ES3_BIT : EGL_OPENGL_ES2_BIT;
    newConfigSpec[len + 1] = EGL10.EGL_NONE;
    return newConfigSpec;
  }

  private int findConfigAttribute(EGL10 egl, EGLDisplay display, EGLConfig config, int attribute, int defaultValue)
  {
    if (egl.eglGetConfigAttrib(display, config, attribute, mValue))
      return mValue[0];
    return defaultValue;
  }
}
