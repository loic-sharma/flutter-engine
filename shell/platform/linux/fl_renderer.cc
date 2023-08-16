// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "fl_renderer.h"

#include <epoxy/egl.h>
#include <epoxy/gl.h>
#include <unordered_map>

#include "flutter/shell/platform/embedder/embedder.h"
#include "flutter/shell/platform/linux/fl_backing_store_provider.h"
#include "flutter/shell/platform/linux/fl_engine_private.h"

G_DEFINE_QUARK(fl_renderer_error_quark, fl_renderer_error)

typedef struct {
  FlView* view;
  // GdkGLContext* visible;

  // target dimension for resizing
  int target_width;
  int target_height;

  // whether the renderer waits for frame render
  bool blocking_main_thread;

  // true if frame was completed; resizing is not synchronized until first frame
  // was rendered
  bool had_first_frame;
} FlRendererView;

typedef struct {
  FlEngine* engine = nullptr;
  GdkGLContext* main_context = nullptr;
  GdkGLContext* resource_context = nullptr;

  std::unordered_map<int64_t, FlRendererView*> views;
} FlRendererPrivate;

G_DEFINE_TYPE_WITH_PRIVATE(FlRenderer, fl_renderer, G_TYPE_OBJECT)

static void fl_renderer_block_main_thread(FlRenderer* self, int64_t view_id) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));
  // Determine if a view has already blocked the main thread.
  bool main_thread_blocked = false;
  for (auto const& pair : priv->views) {
    if (pair.second->blocking_main_thread) {
      main_thread_blocked = true;
      break;
    }
  }

  // Update our state and block the main thread if it isn't already.
  priv->views[view_id]->blocking_main_thread = true;

  if (!main_thread_blocked) {
    FlTaskRunner* runner = fl_engine_get_task_runner(priv->engine);
    fl_task_runner_block_main_thread(runner);
  }
}

static void fl_renderer_unblock_main_thread(FlRenderer* self, int64_t view_id) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));
  // No-op if the view isn't blocking the main thread.
  if (!priv->views[view_id]->blocking_main_thread) {
    return;
  }

  // Update the view's state and unblock the main thread if this was the last
  // view blocking the main thread.
  priv->views[view_id]->blocking_main_thread = false;

  for (auto const& pair : priv->views) {
    if (pair.second->blocking_main_thread) {
      return;
    }
  }

  FlTaskRunner* runner = fl_engine_get_task_runner(priv->engine);
  fl_task_runner_release_main_thread(runner);
}

static void fl_renderer_dispose(GObject* self) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(FL_RENDERER(self)));

  bool unblock_main_thread = false;
  for (auto pair : priv->views) {
    FlRendererView* view = pair.second;
    if (view->blocking_main_thread) {
      unblock_main_thread = true;
    }
    delete view;
  }

  if (unblock_main_thread) {
    FlTaskRunner* runner = fl_engine_get_task_runner(priv->engine);
    fl_task_runner_release_main_thread(runner);
  }
  G_OBJECT_CLASS(fl_renderer_parent_class)->dispose(self);
}

static void fl_renderer_class_init(FlRendererClass* klass) {
  G_OBJECT_CLASS(klass)->dispose = fl_renderer_dispose;
}

static void fl_renderer_init(FlRenderer* self) {}

gboolean fl_renderer_start(FlRenderer* self, FlView* view, GError** error) {
  g_return_val_if_fail(FL_IS_RENDERER(self), FALSE);
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));
  // Initialize the renderer if this is the first view that's added.
  if (priv->engine == nullptr) {
    gboolean result = FL_RENDERER_GET_CLASS(self)->create_contexts(
      self, GTK_WIDGET(view), &priv->main_context, &priv->resource_context,
      error);
    
    if (result) {
      gdk_gl_context_realize(priv->main_context, error);
      gdk_gl_context_realize(priv->resource_context, error);
    }

    if (*error != nullptr) {
      return FALSE;
    }

    priv->engine = fl_view_get_engine(view);
    priv->views = std::unordered_map<int64_t, FlRendererView*>{};
  }

  // GdkWindow* window = gtk_widget_get_parent_window(GTK_WIDGET(view));
  // GdkGLContext* visible = gdk_window_create_gl_context(window, error);

  // if (*error != nullptr) {
  //   return FALSE;
  // }

  FlRendererView* state = new FlRendererView;
  state->view = view;
  // state->visible = visible;
  state->blocking_main_thread = false;
  state->had_first_frame = false;

  int64_t view_id = fl_view_get_id(view);
  priv->views[view_id] = state;
  return TRUE;
}

void fl_renderer_remove(FlRenderer* self, int64_t view_id) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));

  fl_renderer_unblock_main_thread(self, view_id);

  FlRendererView* view = priv->views[view_id];
  priv->views.erase(view_id);
  delete view;
}

FlView* fl_renderer_get_view(FlRenderer* self, int64_t view_id) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));
  return priv->views[view_id]->view;
}

GdkGLContext* fl_renderer_get_context(FlRenderer* self, int64_t view_id) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));
    return priv->main_context;
//  return priv->views[view_id]->visible;
}

void* fl_renderer_get_proc_address(FlRenderer* self, const char* name) {
  return reinterpret_cast<void*>(eglGetProcAddress(name));
}

gboolean fl_renderer_make_current(FlRenderer* self, GError** error) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));
  if (priv->main_context) {
    gdk_gl_context_make_current(priv->main_context);
  }

  return TRUE;
}

gboolean fl_renderer_make_resource_current(FlRenderer* self, GError** error) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));
  if (priv->resource_context) {
    gdk_gl_context_make_current(priv->resource_context);
  }

  return TRUE;
}

gboolean fl_renderer_clear_current(FlRenderer* self, GError** error) {
  gdk_gl_context_clear_current();
  return TRUE;
}

guint32 fl_renderer_get_fbo(FlRenderer* self) {
  // TODO
  // There is only one frame buffer object - always return that.
  return 0;
}

gboolean fl_renderer_create_backing_store(
    FlRenderer* self,
    const FlutterBackingStoreConfig* config,
    FlutterBackingStore* backing_store_out) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));
  if (!priv->main_context) {
    return FALSE;
  }

  return FL_RENDERER_GET_CLASS(self)->create_backing_store(self, config,
                                                           backing_store_out);
}

gboolean fl_renderer_collect_backing_store(
    FlRenderer* self,
    const FlutterBackingStore* backing_store) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));
  if (!priv->main_context) {
    return FALSE;
  }

  return FL_RENDERER_GET_CLASS(self)->collect_backing_store(self,
                                                            backing_store);
}

void fl_renderer_wait_for_frame(FlRenderer* self,
                                int64_t view_id,
                                int target_width,
                                int target_height) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));
  FlRendererView* view = priv->views[view_id];

  view->target_width = target_width;
  view->target_height = target_height;

  if (view->had_first_frame && !view->blocking_main_thread) {
    fl_renderer_block_main_thread(self, view_id);
  }
}

gboolean fl_renderer_present_layers(FlRenderer* self,
                                    const FlutterLayer** layers,
                                    size_t layers_count,
                                    int64_t view_id) {
  FlRendererPrivate* priv = reinterpret_cast<FlRendererPrivate*>(
      fl_renderer_get_instance_private(self));
  FlRendererView* view = priv->views[view_id];

  // ignore incoming frame with wrong dimensions in trivial case with just one
  // layer
  if (view->blocking_main_thread && layers_count == 1 &&
      layers[0]->offset.x == 0 && layers[0]->offset.y == 0 &&
      (layers[0]->size.width != view->target_width ||
       layers[0]->size.height != view->target_height)) {
    return true;
  }

  view->had_first_frame = true;

  fl_renderer_unblock_main_thread(self, view_id);

  return FL_RENDERER_GET_CLASS(self)->present_layers(self, layers,
                                                     layers_count, view_id);
}
