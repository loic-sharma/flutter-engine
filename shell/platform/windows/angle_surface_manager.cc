// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/angle_surface_manager.h"

#include <vector>

#include "flutter/fml/logging.h"

// Logs an EGL error to stderr. This automatically calls eglGetError()
// and logs the error code.
static void LogEglError(std::string message) {
  EGLint error = eglGetError();
  FML_LOG(ERROR) << "EGL: " << message;
  FML_LOG(ERROR) << "EGL: eglGetError returned " << error;
}

namespace flutter {

int AngleSurfaceManager::instance_count_ = 0;

std::unique_ptr<AngleSurfaceManager> AngleSurfaceManager::Create() {
  // TODO(loicsharma): HACK. Forces software rasterization.
  return nullptr;

  std::unique_ptr<AngleSurfaceManager> manager;
  manager.reset(new AngleSurfaceManager());
  if (!manager->initialize_succeeded_) {
    return nullptr;
  }
  return std::move(manager);
}

AngleSurfaceManager::AngleSurfaceManager()
    : egl_config_(nullptr),
      egl_display_(EGL_NO_DISPLAY),
      egl_context_(EGL_NO_CONTEXT) {
  initialize_succeeded_ = Initialize();
  ++instance_count_;
}

AngleSurfaceManager::~AngleSurfaceManager() {
  CleanUp();
  --instance_count_;
}

bool AngleSurfaceManager::InitializeEGL(
    PFNEGLGETPLATFORMDISPLAYEXTPROC egl_get_platform_display_EXT,
    const EGLint* config,
    bool should_log) {
  egl_display_ = egl_get_platform_display_EXT(EGL_PLATFORM_ANGLE_ANGLE,
                                              EGL_DEFAULT_DISPLAY, config);

  if (egl_display_ == EGL_NO_DISPLAY) {
    if (should_log) {
      LogEglError("Failed to get a compatible EGLdisplay");
    }
    return false;
  }

  if (eglInitialize(egl_display_, nullptr, nullptr) == EGL_FALSE) {
    if (should_log) {
      LogEglError("Failed to initialize EGL via ANGLE");
    }
    return false;
  }

  return true;
}

bool AngleSurfaceManager::Initialize() {
  // TODO(dnfield): Enable MSAA here, see similar code in android_context_gl.cc
  // Will need to plumb in argument from project bundle for sampling rate.
  // https://github.com/flutter/flutter/issues/100392
  const EGLint config_attributes[] = {EGL_RED_SIZE,   8, EGL_GREEN_SIZE,   8,
                                      EGL_BLUE_SIZE,  8, EGL_ALPHA_SIZE,   8,
                                      EGL_DEPTH_SIZE, 8, EGL_STENCIL_SIZE, 8,
                                      EGL_NONE};

  const EGLint display_context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                               EGL_NONE};

  // These are preferred display attributes and request ANGLE's D3D11
  // renderer. eglInitialize will only succeed with these attributes if the
  // hardware supports D3D11 Feature Level 10_0+.
  const EGLint d3d11_display_attributes[] = {
      EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,

      // EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE is an option that will
      // enable ANGLE to automatically call the IDXGIDevice3::Trim method on
      // behalf of the application when it gets suspended.
      EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
      EGL_TRUE,

      // This extension allows angle to render directly on a D3D swapchain
      // in the correct orientation on D3D11.
      EGL_EXPERIMENTAL_PRESENT_PATH_ANGLE,
      EGL_EXPERIMENTAL_PRESENT_PATH_FAST_ANGLE,

      EGL_NONE,
  };

  // These are used to request ANGLE's D3D11 renderer, with D3D11 Feature
  // Level 9_3.
  const EGLint d3d11_fl_9_3_display_attributes[] = {
      EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
      EGL_PLATFORM_ANGLE_MAX_VERSION_MAJOR_ANGLE,
      9,
      EGL_PLATFORM_ANGLE_MAX_VERSION_MINOR_ANGLE,
      3,
      EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
      EGL_TRUE,
      EGL_NONE,
  };

  // These attributes request D3D11 WARP (software rendering fallback) in case
  // hardware-backed D3D11 is unavailable.
  const EGLint d3d11_warp_display_attributes[] = {
      EGL_PLATFORM_ANGLE_TYPE_ANGLE,
      EGL_PLATFORM_ANGLE_TYPE_D3D11_ANGLE,
      EGL_PLATFORM_ANGLE_ENABLE_AUTOMATIC_TRIM_ANGLE,
      EGL_TRUE,
      EGL_NONE,
  };

  std::vector<const EGLint*> display_attributes_configs = {
      d3d11_display_attributes,
      d3d11_fl_9_3_display_attributes,
      d3d11_warp_display_attributes,
  };

  PFNEGLGETPLATFORMDISPLAYEXTPROC egl_get_platform_display_EXT =
      reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
          eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (!egl_get_platform_display_EXT) {
    LogEglError("eglGetPlatformDisplayEXT not available");
    return false;
  }

  // Attempt to initialize ANGLE's renderer in order of: D3D11, D3D11 Feature
  // Level 9_3 and finally D3D11 WARP.
  for (auto config : display_attributes_configs) {
    bool should_log = (config == display_attributes_configs.back());
    if (InitializeEGL(egl_get_platform_display_EXT, config, should_log)) {
      break;
    }
  }

  EGLint numConfigs = 0;
  if ((eglChooseConfig(egl_display_, config_attributes, &egl_config_, 1,
                       &numConfigs) == EGL_FALSE) ||
      (numConfigs == 0)) {
    LogEglError("Failed to choose first context");
    return false;
  }

  egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT,
                                  display_context_attributes);
  if (egl_context_ == EGL_NO_CONTEXT) {
    LogEglError("Failed to create EGL context");
    return false;
  }

  egl_resource_context_ = eglCreateContext(
      egl_display_, egl_config_, egl_context_, display_context_attributes);

  if (egl_resource_context_ == EGL_NO_CONTEXT) {
    LogEglError("Failed to create EGL resource context");
    return false;
  }

  return true;
}

void AngleSurfaceManager::CleanUp() {
  EGLBoolean result = EGL_FALSE;

  // Needs to be reset before destroying the EGLContext.
  resolved_device_.Reset();

  if (egl_display_ != EGL_NO_DISPLAY && egl_context_ != EGL_NO_CONTEXT) {
    result = eglDestroyContext(egl_display_, egl_context_);
    egl_context_ = EGL_NO_CONTEXT;

    if (result == EGL_FALSE) {
      LogEglError("Failed to destroy context");
    }
  }

  if (egl_display_ != EGL_NO_DISPLAY &&
      egl_resource_context_ != EGL_NO_CONTEXT) {
    result = eglDestroyContext(egl_display_, egl_resource_context_);
    egl_resource_context_ = EGL_NO_CONTEXT;

    if (result == EGL_FALSE) {
      LogEglError("Failed to destroy resource context");
    }
  }

  if (egl_display_ != EGL_NO_DISPLAY) {
    // Display is reused between instances so only terminate display
    // if destroying last instance
    if (instance_count_ == 1) {
      eglTerminate(egl_display_);
    }
    egl_display_ = EGL_NO_DISPLAY;
  }
}

bool AngleSurfaceManager::CreateSurface(int64_t surface_id,
                                        WindowsRenderTarget* render_target,
                                        EGLint width,
                                        EGLint height,
                                        bool vsync_enabled) {
  FML_DCHECK(!RenderSurfaceExists(surface_id));

  if (!render_target || !initialize_succeeded_) {
    return false;
  }

  EGLSurface surface = EGL_NO_SURFACE;

  const EGLint surfaceAttributes[] = {
      EGL_FIXED_SIZE_ANGLE, EGL_TRUE, EGL_WIDTH, width,
      EGL_HEIGHT,           height,   EGL_NONE};

  surface = eglCreateWindowSurface(
      egl_display_, egl_config_,
      static_cast<EGLNativeWindowType>(std::get<HWND>(*render_target)),
      surfaceAttributes);
  if (surface == EGL_NO_SURFACE) {
    LogEglError("Surface creation failed.");
    return false;
  }

  render_surfaces_.emplace(
      surface_id, std::make_unique<AngleSurface>(surface, width, height));

  SetVSyncEnabled(surface_id, vsync_enabled);
  return true;
}

void AngleSurfaceManager::ResizeSurface(int64_t surface_id,
                                        WindowsRenderTarget* render_target,
                                        EGLint width,
                                        EGLint height,
                                        bool vsync_enabled) {
  FML_DCHECK(RenderSurfaceExists(surface_id));

  EGLint existing_width, existing_height;
  GetSurfaceDimensions(surface_id, &existing_width, &existing_height);
  if (width != existing_width || height != existing_height) {
    ClearContext();
    DestroySurface(surface_id);
    if (!CreateSurface(surface_id, render_target, width, height, vsync_enabled)) {
      FML_LOG(ERROR)
          << "AngleSurfaceManager::ResizeSurface failed to create surface";
    }
  }
}

void AngleSurfaceManager::GetSurfaceDimensions(int64_t surface_id,
                                               EGLint* width,
                                               EGLint* height) {
  if (!initialize_succeeded_ || !RenderSurfaceExists(surface_id)) {
    *width = 0;
    *height = 0;
    return;
  }

  // Can't use eglQuerySurface here; Because we're not using
  // EGL_FIXED_SIZE_ANGLE flag anymore, Angle may resize the surface before
  // Flutter asks it to, which breaks resize redraw synchronization
  *width = render_surfaces_[surface_id]->width;
  *height = render_surfaces_[surface_id]->height;
}

void AngleSurfaceManager::DestroySurface(int64_t surface_id) {
  if (egl_display_ == EGL_NO_DISPLAY || !RenderSurfaceExists(surface_id)) {
    return;
  }

  eglDestroySurface(egl_display_, render_surfaces_[surface_id]->surface);
  render_surfaces_.erase(surface_id);
}

bool AngleSurfaceManager::MakeRenderContextCurrent() {
  return (eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         egl_context_) == EGL_TRUE);
}

bool AngleSurfaceManager::MakeSurfaceCurrent(int64_t surface_id) {
  FML_DCHECK(RenderSurfaceExists(surface_id));

  EGLSurface surface = render_surfaces_[surface_id]->surface;

  return (eglMakeCurrent(egl_display_, surface, surface, egl_context_) ==
          EGL_TRUE);
}

bool AngleSurfaceManager::ClearContext() {
  return (eglMakeCurrent(egl_display_, nullptr, nullptr, egl_context_) ==
          EGL_TRUE);
}

bool AngleSurfaceManager::MakeResourceCurrent() {
  return (eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         egl_resource_context_) == EGL_TRUE);
}

EGLBoolean AngleSurfaceManager::SwapBuffers(int64_t surface_id) {
  FML_DCHECK(RenderSurfaceExists(surface_id));

  return (eglSwapBuffers(egl_display_, render_surfaces_[surface_id]->surface));
}

EGLSurface AngleSurfaceManager::CreateSurfaceFromHandle(
    EGLenum handle_type,
    EGLClientBuffer handle,
    const EGLint* attributes) const {
  return eglCreatePbufferFromClientBuffer(egl_display_, handle_type, handle,
                                          egl_config_, attributes);
}

bool AngleSurfaceManager::RenderSurfaceExists(int64_t surface_id) {
  return render_surfaces_.find(surface_id) != render_surfaces_.end();
}

void AngleSurfaceManager::SetVSyncEnabled(int64_t surface_id, bool enabled) {
  FML_DCHECK(RenderSurfaceExists(surface_id));

  EGLSurface surface = render_surfaces_[surface_id]->surface;
  if (eglMakeCurrent(egl_display_, surface, surface, egl_context_) !=
      EGL_TRUE) {
    LogEglError("Unable to make surface current to update the swap interval");
    return;
  }

  // OpenGL swap intervals can be used to prevent screen tearing
  // by blocking the raster thread blocks until the v-blank.
  // This is unnecessary if DWM composition is enabled.
  // See: https://www.khronos.org/opengl/wiki/Swap_Interval
  // See: https://learn.microsoft.com/windows/win32/dwm/composition-ovw
  if (eglSwapInterval(egl_display_, enabled ? 1 : 0) != EGL_TRUE) {
    LogEglError("Unable to update the swap interval");
    return;
  }
}

bool AngleSurfaceManager::GetDevice(ID3D11Device** device) {
  if (!resolved_device_) {
    PFNEGLQUERYDISPLAYATTRIBEXTPROC egl_query_display_attrib_EXT =
        reinterpret_cast<PFNEGLQUERYDISPLAYATTRIBEXTPROC>(
            eglGetProcAddress("eglQueryDisplayAttribEXT"));

    PFNEGLQUERYDEVICEATTRIBEXTPROC egl_query_device_attrib_EXT =
        reinterpret_cast<PFNEGLQUERYDEVICEATTRIBEXTPROC>(
            eglGetProcAddress("eglQueryDeviceAttribEXT"));

    if (!egl_query_display_attrib_EXT || !egl_query_device_attrib_EXT) {
      return false;
    }

    EGLAttrib egl_device = 0;
    EGLAttrib angle_device = 0;
    if (egl_query_display_attrib_EXT(egl_display_, EGL_DEVICE_EXT,
                                     &egl_device) == EGL_TRUE) {
      if (egl_query_device_attrib_EXT(
              reinterpret_cast<EGLDeviceEXT>(egl_device),
              EGL_D3D11_DEVICE_ANGLE, &angle_device) == EGL_TRUE) {
        resolved_device_ = reinterpret_cast<ID3D11Device*>(angle_device);
      }
    }
  }

  resolved_device_.CopyTo(device);
  return (resolved_device_ != nullptr);
}

}  // namespace flutter
