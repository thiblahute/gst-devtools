/* GStreamer
 *
 * Copyright (C) 2014 Sebastian Dröge <sebastian@centricular.com>
 * Copyright (C) 2014 Thibault Saunier <thibault.saunier@collabora.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifndef __GST_LAUNCH_REMOTE_H__
#define __GST_LAUNCH_REMOTE_H__

#include <gio/gio.h>
#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-scenario.h>
#include <gst/validate/gst-validate-utils.h>

#define PORT 9123

typedef struct {
  gpointer app;
  void (*set_message) (const gchar *message, gpointer app);
  void (*initialized) (gpointer app);
  void (*media_size_changed) (gint width, gint height, gpointer app);
  void (*pipeline_done) (gpointer app);
} GstValidateAndroidAppContext;

typedef struct {
  GThread *thread;
  GMainContext *context;
  GMainLoop *main_loop;
  gchar *args;

  guintptr window_handle;

  gboolean initialized;
  gboolean validate_initialized;

  gchar *pipeline_string;
  GstElement *pipeline;
  GstElement *video_sink;
  GstState target_state;
  gboolean is_live;

  GSocketService *service;
  GSocketConnection *connection;
  GDataInputStream *distream;
  GOutputStream *ostream;
  GSocket *debug_socket;

  GstValidateAndroidAppContext app_context;

  GstValidateRunner *runner;
  GstValidateMonitor *monitor;
} GstValidateAndroid;

/* Set callbacks manually as required */
GstValidateAndroid * gst_validate_android_new               (const GstValidateAndroidAppContext *ctx);
void              gst_validate_android_free              (GstValidateAndroid * self);
void              gst_validate_android_set_window_handle (GstValidateAndroid * self, guintptr handle);
void              gst_validate_android_set_parametters        (GstValidateAndroid *self, gchar *args);


#endif
