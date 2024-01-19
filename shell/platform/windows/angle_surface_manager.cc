// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "flutter/shell/platform/windows/angle_surface_manager.h"

#include <vector>

#include "flutter/fml/logging.h"

namespace flutter {

namespace {

using GetPlatformDisplayEXT = EGLDisplay(_stdcall*)(EGLenum platform,
                                                    void* native_display,
                                                    const EGLint* attrib_list);
using QueryDisplayAttribEXT = EGLBoolean(_stdcall*)(EGLDisplay display,
                                                    EGLint attribute,
                                                    EGLAttrib* value);
using QueryDeviceAttribEXT = EGLBoolean(__stdcall*)(EGLDeviceEXT device,
                                                    EGLint attribute,
                                                    EGLAttrib* value);

const char* EGLErrorToString(EGLint error) {
  switch (error) {
    case EGL_SUCCESS:
      return "Success";
    case EGL_NOT_INITIALIZED:
      return "Not Initialized";
    case EGL_BAD_ACCESS:
      return "Bad Access";
    case EGL_BAD_ALLOC:
      return "Bad Alloc";
    case EGL_BAD_ATTRIBUTE:
      return "Bad Attribute";
    case EGL_BAD_CONTEXT:
      return "Bad Context";
    case EGL_BAD_CONFIG:
      return "Bad Config";
    case EGL_BAD_CURRENT_SURFACE:
      return "Bad Current Surface";
    case EGL_BAD_DISPLAY:
      return "Bad Display";
    case EGL_BAD_SURFACE:
      return "Bad Surface";
    case EGL_BAD_MATCH:
      return "Bad Match";
    case EGL_BAD_PARAMETER:
      return "Bad Parameter";
    case EGL_BAD_NATIVE_PIXMAP:
      return "Bad Native Pixmap";
    case EGL_BAD_NATIVE_WINDOW:
      return "Bad Native Window";
    case EGL_CONTEXT_LOST:
      return "Context Lost";
  }
  return "Unknown";
}

void LogEGLError(std::string message) {
  const auto error = ::eglGetError();
  return FML_LOG(ERROR) << "EGL Error: " << EGLErrorToString(error) << " ("
                        << error << ") " << message;
}

void LogEGLError(const char* file, int line) {
  std::stringstream stream;
  stream << "in " << file << ":" << line;
  LogEGLError(stream.str());
}

#define WINDOWS_LOG_EGL_ERROR LogEGLError(__FILE__, __LINE__);

}  // namespace

int AngleSurfaceManager::instance_count_ = 0;

std::unique_ptr<AngleSurfaceManager> AngleSurfaceManager::Create(
    bool enable_impeller) {
  std::unique_ptr<AngleSurfaceManager> manager;
  manager.reset(new AngleSurfaceManager(enable_impeller));
  if (!manager->initialize_succeeded_) {
    return nullptr;
  }
  return std::move(manager);
}

AngleSurfaceManager::AngleSurfaceManager(bool enable_impeller)
    : egl_config_(nullptr),
      egl_display_(EGL_NO_DISPLAY),
      egl_context_(EGL_NO_CONTEXT) {
  initialize_succeeded_ = Initialize(enable_impeller);
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
      LogEGLError("Failed to get a compatible EGLdisplay");
    }
    return false;
  }

  if (eglInitialize(egl_display_, nullptr, nullptr) == EGL_FALSE) {
    if (should_log) {
      LogEGLError("Failed to initialize EGL via ANGLE");
    }
    return false;
  }

  return true;
}

bool AngleSurfaceManager::Initialize(bool enable_impeller) {
  const EGLint config_attributes[] = {EGL_RED_SIZE,   8, EGL_GREEN_SIZE,   8,
                                      EGL_BLUE_SIZE,  8, EGL_ALPHA_SIZE,   8,
                                      EGL_DEPTH_SIZE, 8, EGL_STENCIL_SIZE, 8,
                                      EGL_NONE};

  const EGLint impeller_config_attributes[] = {
      EGL_RED_SIZE,       8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE,    8,
      EGL_ALPHA_SIZE,     8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 8,
      EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES,    4, EGL_NONE};
  const EGLint impeller_config_attributes_no_msaa[] = {
      EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE,    8,
      EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 8,
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
    LogEGLError("eglGetPlatformDisplayEXT not available");
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
  if (enable_impeller) {
    // First try the MSAA configuration.
    if ((eglChooseConfig(egl_display_, impeller_config_attributes, &egl_config_,
                         1, &numConfigs) == EGL_FALSE) ||
        (numConfigs == 0)) {
      // Next fall back to disabled MSAA.
      if ((eglChooseConfig(egl_display_, impeller_config_attributes_no_msaa,
                           &egl_config_, 1, &numConfigs) == EGL_FALSE) ||
          (numConfigs == 0)) {
        LogEGLError("Failed to choose first context");
        return false;
      }
    }
  } else {
    if ((eglChooseConfig(egl_display_, config_attributes, &egl_config_, 1,
                         &numConfigs) == EGL_FALSE) ||
        (numConfigs == 0)) {
      LogEGLError("Failed to choose first context");
      return false;
    }
  }

  egl_context_ = eglCreateContext(egl_display_, egl_config_, EGL_NO_CONTEXT,
                                  display_context_attributes);
  if (egl_context_ == EGL_NO_CONTEXT) {
    LogEGLError("Failed to create EGL context");
    return false;
  }

  egl_resource_context_ = eglCreateContext(
      egl_display_, egl_config_, egl_context_, display_context_attributes);

  if (egl_resource_context_ == EGL_NO_CONTEXT) {
    LogEGLError("Failed to create EGL resource context");
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
      LogEGLError("Failed to destroy context");
    }
  }

  if (egl_display_ != EGL_NO_DISPLAY &&
      egl_resource_context_ != EGL_NO_CONTEXT) {
    result = eglDestroyContext(egl_display_, egl_resource_context_);
    egl_resource_context_ = EGL_NO_CONTEXT;

    if (result == EGL_FALSE) {
      LogEGLError("Failed to destroy resource context");
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

bool AngleSurfaceManager::CreateSurface(HWND hwnd,
                                        EGLint width,
                                        EGLint height) {
  if (!hwnd || !initialize_succeeded_) {
    return false;
  }

  EGLSurface surface = EGL_NO_SURFACE;

  // Disable ANGLE's automatic surface resizing and provide an explicit size.
  // The surface will need to be destroyed and re-created if the HWND is
  // resized.
  const EGLint surfaceAttributes[] = {
      EGL_FIXED_SIZE_ANGLE, EGL_TRUE, EGL_WIDTH, width,
      EGL_HEIGHT,           height,   EGL_NONE};

  surface = eglCreateWindowSurface(egl_display_, egl_config_,
                                   static_cast<EGLNativeWindowType>(hwnd),
                                   surfaceAttributes);
  if (surface == EGL_NO_SURFACE) {
    LogEGLError("Surface creation failed.");
    return false;
  }

  surface_width_ = width;
  surface_height_ = height;
  render_surface_ = surface;
  return true;
}

void AngleSurfaceManager::ResizeSurface(HWND hwnd,
                                        EGLint width,
                                        EGLint height,
                                        bool vsync_enabled) {
  EGLint existing_width, existing_height;
  GetSurfaceDimensions(&existing_width, &existing_height);
  if (width != existing_width || height != existing_height) {
    surface_width_ = width;
    surface_height_ = height;

    // TODO: Destroying the surface and re-creating it is expensive.
    // Ideally this would use ANGLE's automatic surface sizing instead.
    // See: https://github.com/flutter/flutter/issues/79427
    ClearContext();
    DestroySurface();
    if (!CreateSurface(hwnd, width, height)) {
      FML_LOG(ERROR)
          << "AngleSurfaceManager::ResizeSurface failed to create surface";
    }
  }

  SetVSyncEnabled(vsync_enabled);
}

void AngleSurfaceManager::GetSurfaceDimensions(EGLint* width, EGLint* height) {
  if (render_surface_ == EGL_NO_SURFACE || !initialize_succeeded_) {
    *width = 0;
    *height = 0;
    return;
  }

  // This avoids eglQuerySurface as ideally surfaces would be automatically
  // sized by ANGLE to avoid expensive surface destroy & re-create. With
  // automatic sizing, ANGLE could resize the surface before Flutter asks it to,
  // which would break resize redraw synchronization.
  *width = surface_width_;
  *height = surface_height_;
}

void AngleSurfaceManager::DestroySurface() {
  if (egl_display_ != EGL_NO_DISPLAY && render_surface_ != EGL_NO_SURFACE) {
    eglDestroySurface(egl_display_, render_surface_);
  }
  render_surface_ = EGL_NO_SURFACE;
}

bool AngleSurfaceManager::HasContextCurrent() {
  return eglGetCurrentContext() != EGL_NO_CONTEXT;
}

bool AngleSurfaceManager::MakeCurrent() {
  return (eglMakeCurrent(egl_display_, render_surface_, render_surface_,
                         egl_context_) == EGL_TRUE);
}

bool AngleSurfaceManager::ClearCurrent() {
  return (eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         EGL_NO_CONTEXT) == EGL_TRUE);
}

bool AngleSurfaceManager::ClearContext() {
  return (eglMakeCurrent(egl_display_, nullptr, nullptr, egl_context_) ==
          EGL_TRUE);
}

bool AngleSurfaceManager::MakeResourceCurrent() {
  return (eglMakeCurrent(egl_display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                         egl_resource_context_) == EGL_TRUE);
}

bool AngleSurfaceManager::SwapBuffers() {
  return (eglSwapBuffers(egl_display_, render_surface_));
}

EGLSurface AngleSurfaceManager::CreateSurfaceFromHandle(
    EGLenum handle_type,
    EGLClientBuffer handle,
    const EGLint* attributes) const {
  return eglCreatePbufferFromClientBuffer(egl_display_, handle_type, handle,
                                          egl_config_, attributes);
}

void AngleSurfaceManager::SetVSyncEnabled(bool enabled) {
  if (!MakeCurrent()) {
    LogEGLError("Unable to make surface current to update the swap interval");
    return;
  }

  // OpenGL swap intervals can be used to prevent screen tearing.
  // If enabled, the raster thread blocks until the v-blank.
  // This is unnecessary if DWM composition is enabled.
  // See: https://www.khronos.org/opengl/wiki/Swap_Interval
  // See: https://learn.microsoft.com/windows/win32/dwm/composition-ovw
  if (eglSwapInterval(egl_display_, enabled ? 1 : 0) != EGL_TRUE) {
    LogEGLError("Unable to update the swap interval");
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

std::unique_ptr<WindowsEGLManager> WindowsEGLManager::Create(
    bool enable_impeller) {
  std::unique_ptr<WindowsEGLManager> manager;
  manager.reset(new WindowsEGLManager(enable_impeller));
  if (!manager->IsValid()) {
    return nullptr;
  }
  return std::move(manager);
}

WindowsEGLManager::WindowsEGLManager(bool enable_impeller) {
  if (!InitializeDisplay()) {
    return;
  }

  if (!InitializeConfig(enable_impeller)) {
    return;
  }

  if (!InitializeContexts()) {
    return;
  }

  is_valid_ = true;
}

bool WindowsEGLManager::InitializeDisplay() {
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

  const auto get_platform_display_EXT = reinterpret_cast<GetPlatformDisplayEXT>(
      ::eglGetProcAddress("eglGetPlatformDisplayEXT"));
  if (!get_platform_display_EXT) {
    WINDOWS_LOG_EGL_ERROR;
    return;
  }

  // Attempt to initialize ANGLE's renderer in order of: D3D11, D3D11 Feature
  // Level 9_3 and finally D3D11 WARP.
  for (auto config : display_attributes_configs) {
    bool is_last = config == display_attributes_configs.back();

    display_ = get_platform_display_EXT(EGL_PLATFORM_ANGLE_ANGLE,
                                        EGL_DEFAULT_DISPLAY, config);

    if (display_ == EGL_NO_DISPLAY) {
      if (is_last) {
        LogEGLError("Failed to get a compatible EGLDisplay");
      }

      continue;
    }

    if (::eglInitialize(display_, nullptr, nullptr) == EGL_FALSE) {
      if (is_last) {
        LogEGLError("Failed to initialize EGL via ANGLE");
      }

      continue;
    }

    return true;
  }

  return false;
}

bool WindowsEGLManager::InitializeConfig(bool enable_impeller) {
  const EGLint config_attributes[] = {EGL_RED_SIZE,   8, EGL_GREEN_SIZE,   8,
                                      EGL_BLUE_SIZE,  8, EGL_ALPHA_SIZE,   8,
                                      EGL_DEPTH_SIZE, 8, EGL_STENCIL_SIZE, 8,
                                      EGL_NONE};

  const EGLint impeller_config_attributes[] = {
      EGL_RED_SIZE,       8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE,    8,
      EGL_ALPHA_SIZE,     8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 8,
      EGL_SAMPLE_BUFFERS, 1, EGL_SAMPLES,    4, EGL_NONE};
  const EGLint impeller_config_attributes_no_msaa[] = {
      EGL_RED_SIZE,   8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE,    8,
      EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 8,
      EGL_NONE};

  EGLBoolean result;
  EGLint num_config = 0;

  if (enable_impeller) {
    // First try the MSAA configuration.
    result = ::eglChooseConfig(display_, impeller_config_attributes, &config_,
                               1, &num_config);

    if (result == EGL_TRUE && num_config > 0) {
      return true;
    }

    // Next fall back to disabled MSAA.
    result = ::eglChooseConfig(display_, impeller_config_attributes_no_msaa,
                               &config_, 1, &num_config);
    if (result == EGL_TRUE && num_config == 0) {
      return true;
    }
  } else {
    result = ::eglChooseConfig(display_, config_attributes, &config_, 1,
                               &num_config);

    if (result == EGL_TRUE && num_config > 0) {
      return true;
    }
  }

  LogEGLError("Failed to choose EGL config");
  return false;
}

bool WindowsEGLManager::InitializeContexts() {
  const EGLint context_attributes[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};

  auto const render_context =
      ::eglCreateContext(display_, config_, EGL_NO_CONTEXT, context_attributes);
  if (render_context == EGL_NO_CONTEXT) {
    LogEGLError("Failed to create EGL render context");
    return false;
  }

  auto const resource_context =
      ::eglCreateContext(display_, config_, render_context, context_attributes);
  if (resource_context == EGL_NO_CONTEXT) {
    LogEGLError("Failed to create EGL resource context");
    return false;
  }

  render_context_ =
      std::make_unique<WindowsEGLContext>(display_, render_context);
  resource_context_ =
      std::make_unique<WindowsEGLContext>(display_, resource_context);
  return true;
}

bool WindowsEGLManager::InitializeDevice() {
  const auto query_display_attrib_EXT = reinterpret_cast<QueryDisplayAttribEXT>(
      ::eglGetProcAddress("eglQueryDisplayAttribEXT"));
  const auto query_device_attrib_EXT = reinterpret_cast<QueryDeviceAttribEXT>(
      ::eglGetProcAddress("eglQueryDeviceAttribEXT"));

  if (query_display_attrib_EXT == nullptr ||
      query_device_attrib_EXT == nullptr) {
    return false;
  }

  EGLBoolean result;
  EGLAttrib egl_device = 0;
  EGLAttrib angle_device = 0;

  auto result = query_display_attrib_EXT(display_, EGL_DEVICE_EXT, &egl_device);
  if (result != EGL_TRUE) {
    return false;
  }

  result = query_device_attrib_EXT(reinterpret_cast<EGLDeviceEXT>(egl_device),
                                   EGL_D3D11_DEVICE_ANGLE, &angle_device);
  if (result != EGL_TRUE) {
    return false;
  }

  resolved_device_ = reinterpret_cast<ID3D11Device*>(angle_device);
}

bool WindowsEGLManager::IsValid() const {
  return is_valid_;
}

bool WindowsEGLManager::CreateSurface(HWND hwnd, size_t width, size_t height) {
  if (is_valid_) {
    return false;
  }

  surface_ = std::make_unique<WindowsEGLSurface>(config_, display_,
                                                 render_context_->GetHandle());
  if (!surface_->Create(hwnd, width, height)) {
    return false;
  }

  return true;
}

EGLSurface WindowsEGLManager::CreateSurfaceFromHandle(
    EGLenum handle_type,
    EGLClientBuffer handle,
    const EGLint* attributes) const {
  const auto result = ::eglCreatePbufferFromClientBuffer(
      display_, handle_type, handle, config_, attributes);
  if (result == EGL_NO_SURFACE) {
    WINDOWS_LOG_EGL_ERROR;
  }
  return result;
}

bool WindowsEGLManager::ClearCurrent() {
  if (eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE,
                     EGL_NO_CONTEXT) != EGL_TRUE) {
    WINDOWS_LOG_EGL_ERROR;
    return false;
  }

  return true;
}

bool WindowsEGLManager::GetDevice(ID3D11Device** device) {
  if (!resolved_device_) {
    if (!InitializeDevice()) {
      return false;
    }
  }

  resolved_device_.CopyTo(device);
  return resolved_device_ != nullptr;
}

const EGLDisplay& WindowsEGLManager::display() const {
  return display_;
}

WindowsEGLContext* WindowsEGLManager::render_context() const {
  return render_context_.get();
}

WindowsEGLContext* WindowsEGLManager::resource_context() const {
  return resource_context_.get();
}

WindowsEGLSurface* WindowsEGLManager::surface() const {
  return surface_.get();
}

WindowsEGLContext::WindowsEGLContext(EGLDisplay display, EGLContext context)
    : display_(display), context_(context) {}

WindowsEGLContext::~WindowsEGLContext() {
  if (display_ == EGL_NO_DISPLAY && context_ == EGL_NO_CONTEXT) {
    return;
  }

  if (::eglDestroyContext(display_, context_) != EGL_TRUE) {
    WINDOWS_LOG_EGL_ERROR;
  }
}

bool WindowsEGLContext::IsValid() const {
  return is_valid_;
}

bool WindowsEGLContext::IsCurrent() const {
  return ::eglGetCurrentContext() == context_;
}

bool WindowsEGLContext::MakeCurrent() const {
  const auto result =
      ::eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, context_);
  if (result != EGL_TRUE) {
    WINDOWS_LOG_EGL_ERROR;
    return false;
  }

  return true;
}

const EGLContext& WindowsEGLContext::GetHandle() const {
  return context_;
}

WindowsEGLSurface::WindowsEGLSurface(EGLConfig config,
                                     EGLDisplay display,
                                     EGLContext context)
    : config_(config), display_(display), context_(context) {}

WindowsEGLSurface::~WindowsEGLSurface() {
  Destroy();
}

bool WindowsEGLSurface::IsValid() const {
  return is_valid_;
}

bool WindowsEGLSurface::Create(HWND hwnd, int32_t width, int32_t height) {
  // Disable ANGLE's automatic surface resizing and provide an explicit size.
  // The surface will need to be destroyed and re-created if the HWND is
  // resized.
  const EGLint surfaceAttributes[] = {
      EGL_FIXED_SIZE_ANGLE, EGL_TRUE, EGL_WIDTH, width,
      EGL_HEIGHT,           height,   EGL_NONE};

  surface_ = ::eglCreateWindowSurface(display_, config_,
                                      static_cast<EGLNativeWindowType>(hwnd),
                                      surfaceAttributes);
  if (surface_ == EGL_NO_SURFACE) {
    WINDOWS_LOG_EGL_ERROR;
    return false;
  }

  width_ = width;
  height_ = height;
  is_valid_ = true;
  return true;
}

bool WindowsEGLSurface::Destroy() {
  if (surface_ != EGL_NO_SURFACE) {
    // Ensure the surface is not current before destroying it.
    if (::eglMakeCurrent(display_, nullptr, nullptr, context_) != EGL_TRUE) {
      WINDOWS_LOG_EGL_ERROR;
      return false;
    }

    if (::eglDestroySurface(display_, surface_) != EGL_TRUE) {
      WINDOWS_LOG_EGL_ERROR;
      return false;
    }
  }

  is_valid_ = false;
  surface_ = EGL_NO_SURFACE;
  return true;
}

bool WindowsEGLSurface::MakeCurrent() const {
  if (::eglMakeCurrent(display_, surface_, surface_, context_) != EGL_TRUE) {
    WINDOWS_LOG_EGL_ERROR;
    return false;
  }

  return true;
}

bool WindowsEGLSurface::SwapBuffers() const {
  if (::eglSwapBuffers(display_, surface_) != EGL_TRUE) {
    WINDOWS_LOG_EGL_ERROR;
    return false;
  }

  return true;
}

bool WindowsEGLSurface::SetVsyncEnabled(bool enabled) {
  if (!MakeCurrent()) {
    return false;
  }

  if (::eglSwapInterval(display_, enabled ? 1 : 0) != EGL_TRUE) {
    WINDOWS_LOG_EGL_ERROR;
    return false;
  }

  vsync_enabled_ = enabled;
  return true;
}

bool WindowsEGLSurface::Resize(HWND hwnd, int32_t width, int32_t height) {
  if (width == width_ && height == height_) {
    return true;
  }

  // Destroy the surface and re-create it.
  if (Destroy()) {
    return false;
  }

  if (!Create(hwnd, width, height)) {
    return false;
  }

  if (!SetVsyncEnabled(vsync_enabled_)) {
    return false;
  }

  return true;
}

const EGLSurface& WindowsEGLSurface::GetHandle() const {
  return surface_;
}

int32_t WindowsEGLSurface::width() const {
  return width_;
}

int32_t WindowsEGLSurface::height() const {
  return height_;
}

}  // namespace flutter
