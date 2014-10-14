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

#include "gst-validate-android.h"

#include "../../gst-validate.h"
#include "../../gst-validate-transcoding.h"
#include "gst-inspect.h"

#include <string.h>
#include <stdlib.h>
#include <android/log.h>
#include <glib/gprintf.h>       /* g_sprintf */

#include <signal.h>
#include <glib-unix.h>
#include <sys/wait.h>

GST_DEBUG_CATEGORY_STATIC (debug_category);
#define GST_CAT_DEFAULT debug_category

G_LOCK_DEFINE (context_exists);

static gboolean no_color = FALSE;

static void
fault_spin (void)
{
  int spinning = TRUE;

  wait (NULL);

  while (spinning) {
    g_print ("Spinning. Please run 'ndk-gdb --verbose --force' from"
        " the gst-devtools/validate/tools/android folder, Ctrl-C to quit.");

    g_usleep (G_USEC_PER_SEC * 10);
  }
}

static void
fault_handler_sighandler (int signum)
{
  /* printf is used instead of g_print(), since it's less likely to
   * deadlock */
  switch (signum) {
    case SIGSEGV:
      g_print ("Caught SIGSEGV\n");
      break;
    case SIGQUIT:
      g_print ("Caught SIGQUIT\n");
      break;
    default:
      g_print ("signo:  %d\n", signum);
      break;
  }

  fault_spin ();
}

static void
fault_setup (void)
{
  struct sigaction action;

  memset (&action, 0, sizeof (action));
  action.sa_handler = fault_handler_sighandler;

  sigaction (SIGSEGV, &action, NULL);
  sigaction (SIGQUIT, &action, NULL);
}

void
priv_glib_print_handler (const gchar * string)
{
  __android_log_print (ANDROID_LOG_INFO, "GLib", string);
}

void
priv_glib_printerr_handler (const gchar * string)
{
  __android_log_print (ANDROID_LOG_ERROR, "GLib", string);
}

void
priv_validate_print (GString * string)
{
  __android_log_print (ANDROID_LOG_ERROR, "GstValidateOutput", string->str);
}

/* Based on GLib's default handler */
#define CHAR_IS_SAFE(wc) (!((wc < 0x20 && wc != '\t' && wc != '\n' && wc != '\r') || \
			    (wc == 0x7f) || \
			    (wc >= 0x80 && wc < 0xa0)))
#define FORMAT_UNSIGNED_BUFSIZE ((GLIB_SIZEOF_LONG * 3) + 3)
#define	STRING_BUFFER_SIZE	(FORMAT_UNSIGNED_BUFSIZE + 32)
#define	ALERT_LEVELS		(G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING)
#define DEFAULT_LEVELS (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL | G_LOG_LEVEL_WARNING | G_LOG_LEVEL_MESSAGE)
#define INFO_LEVELS (G_LOG_LEVEL_INFO | G_LOG_LEVEL_DEBUG)

static void
escape_string (GString * string)
{
  const char *p = string->str;
  gunichar wc;

  while (p < string->str + string->len) {
    gboolean safe;

    wc = g_utf8_get_char_validated (p, -1);
    if (wc == (gunichar) - 1 || wc == (gunichar) - 2) {
      gchar *tmp;
      guint pos;

      pos = p - string->str;

      /* Emit invalid UTF-8 as hex escapes 
       */
      tmp = g_strdup_printf ("\\x%02x", (guint) (guchar) * p);
      g_string_erase (string, pos, 1);
      g_string_insert (string, pos, tmp);

      p = string->str + (pos + 4);      /* Skip over escape sequence */

      g_free (tmp);
      continue;
    }
    if (wc == '\r') {
      safe = *(p + 1) == '\n';
    } else {
      safe = CHAR_IS_SAFE (wc);
    }

    if (!safe) {
      gchar *tmp;
      guint pos;

      pos = p - string->str;

      /* Largest char we escape is 0x0a, so we don't have to worry
       * about 8-digit \Uxxxxyyyy
       */
      tmp = g_strdup_printf ("\\u%04x", wc);
      g_string_erase (string, pos, g_utf8_next_char (p) - p);
      g_string_insert (string, pos, tmp);
      g_free (tmp);

      p = string->str + (pos + 6);      /* Skip over escape sequence */
    } else
      p = g_utf8_next_char (p);
  }
}

void
priv_glib_log_handler (const gchar * log_domain, GLogLevelFlags log_level,
    const gchar * message, gpointer user_data)
{
  gchar *string;
  GString *gstring;
  const gchar *domains;
  const gchar *level;
  gchar *tag;

  if ((log_level & DEFAULT_LEVELS) || (log_level >> G_LOG_LEVEL_USER_SHIFT))
    goto emit;

  domains = g_getenv ("G_MESSAGES_DEBUG");
  if (((log_level & INFO_LEVELS) == 0) ||
      domains == NULL ||
      (strcmp (domains, "all") != 0 && (!log_domain
              || !strstr (domains, log_domain))))
    return;

emit:

  switch (log_level & G_LOG_LEVEL_MASK) {
    case G_LOG_LEVEL_ERROR:
      level = "ERROR";
      break;
    case G_LOG_LEVEL_CRITICAL:
      level = "CRITICAL";
      break;
    case G_LOG_LEVEL_WARNING:
      level = "WARNING";
      break;
    case G_LOG_LEVEL_MESSAGE:
      level = "MESSAGE";
      break;
    case G_LOG_LEVEL_INFO:
      level = "INFO";
      break;
    case G_LOG_LEVEL_DEBUG:
      level = "DEBUG";
      break;
    default:
      level = "DEBUG";
      break;
  }

  if (log_domain)
    tag = g_strdup_printf ("%s (%s) ", log_domain, level);
  else
    tag = g_strdup_printf ("(%s) ", level);

  gstring = g_string_new (tag);
  if (!message) {
    g_string_append (gstring, "(NULL) message");
  } else {
    GString *msg = g_string_new (message);
    escape_string (msg);
    g_string_append (gstring, msg->str);
    g_string_free (msg, TRUE);
  }
  string = g_string_free (gstring, FALSE);

  priv_glib_printerr_handler (string);

  g_free (string);
  g_free (tag);
}

static GstClockTime start_time;
static const gchar *levelcolormap[GST_LEVEL_COUNT] = {
  "\033[37m",                   /* GST_LEVEL_NONE */
  "\033[31;01m",                /* GST_LEVEL_ERROR */
  "\033[33;01m",                /* GST_LEVEL_WARNING */
  "\033[32;01m",                /* GST_LEVEL_INFO */
  "\033[36m",                   /* GST_LEVEL_DEBUG */
  "\033[37m",                   /* GST_LEVEL_LOG */
  "\033[33;01m",                /* GST_LEVEL_FIXME */
  "\033[37m",                   /* GST_LEVEL_TRACE */
  "\033[37m",                   /* placeholder for log level 8 */
  "\033[37m"                    /* GST_LEVEL_MEMDUMP */
};

void
priv_gst_debug_logcat (GstDebugCategory * category, GstDebugLevel level,
    const gchar * file, const gchar * function, gint line,
    GObject * object, GstDebugMessage * message, gpointer unused)
{
  gint pid;
  gchar *obj;
  GstClockTime elapsed;
  const gchar *level_str;
  gchar *m;
  gchar *color = NULL;
  const gchar *clear;
  const gchar *levelcolor;

  if (level > gst_debug_category_get_threshold (category))
    return;

  elapsed = GST_CLOCK_DIFF (start_time, gst_util_get_timestamp ());

  switch (level) {
    case GST_LEVEL_ERROR:
      level_str = "ERROR";
      break;
    case GST_LEVEL_WARNING:
      level_str = "WARNING";
      break;
    case GST_LEVEL_INFO:
      level_str = "INFO";
      break;
    case GST_LEVEL_DEBUG:
      level_str = "DEBUG";
      break;
    default:
      level_str = "OTHER";
      break;
  }

  if (no_color) {
    color = g_strdup ("");
    clear = "";
    levelcolor = "";
  } else {
    color = gst_debug_construct_term_color (gst_debug_category_get_color
        (category));
    clear = "\033[00m";
    levelcolor = levelcolormap[level];
  }

  if (object) {
    if (GST_IS_PAD (object) && GST_OBJECT_NAME (object)) {
      obj = g_strdup_printf ("<%s:%s>", GST_DEBUG_PAD_NAME (object));
    } else if (GST_IS_OBJECT (object) && GST_OBJECT_NAME (object)) {
      obj = g_strdup_printf ("<%s>", GST_OBJECT_NAME (object));
    } else if (G_IS_OBJECT (object)) {
      obj = g_strdup_printf ("<%s@%p>", G_OBJECT_TYPE_NAME (object), object);
    } else {
      obj = g_strdup_printf ("<%p>", object);
    }

  } else {
    obj = g_strdup ("");
  }

  m = g_strdup_printf ("%" GST_TIME_FORMAT
      "      %p      %s%s%s      %s%s%s %s:%d:%s:%s %s",
      GST_TIME_ARGS (elapsed), g_thread_self (), levelcolor, level_str, clear,
      color, gst_debug_category_get_name (category), clear, file, line,
      function, obj, gst_debug_message_get (message));

  __android_log_print (ANDROID_LOG_ERROR, "GStreamer", m);
  g_free (m);
  g_free (color);
  g_free (obj);
}

static void
set_message (GstValidateAndroid * self, const gchar * format, ...)
{
  va_list args;
  gchar *message;

  if (!self->app_context.set_message) {
    return;
  }

  if (format) {
    if (self->message)
      g_string_free (self->message, TRUE);

    if (self->is_transcoder)
      self->message = g_string_new ("Transcoding pipeline\n");
    else
      self->message = g_string_new (NULL);

    va_start (args, format);
    g_string_append_vprintf (self->message, format, args);
    va_end (args);
  }

  if (self->position)
    message = g_strdup_printf ("%s -- %s", self->message->str, self->position);
  else
    message = g_strdup (self->message->str);

  self->app_context.set_message (message, self->app_context.app);

  g_free (message);
}

static void
set_position (GstValidateAndroid * self, const gchar * format, ...)
{
  va_list args;

  g_free (self->position);

  va_start (args, format);
  self->position = g_strdup_vprintf (format, args);
  va_end (args);

  set_message (self, NULL, NULL);
}

static void
__fake_exit (GstValidateAndroid * self, gint returncode, gchar * message)
{
  gchar *msg;

  if (message)
    msg = g_strdup_printf (" (%s)", message);
  else
    msg = g_strdup ("");

  gst_validate_printf (NULL, "<RETURN: %d%s />", returncode, msg);
  g_free (msg);

  self->app_context.pipeline_done (self->app_context.app);
}

static void
gst_validate_android_clean_pipeline (GstValidateAndroid * self)
{
  gint ret = 0;
  const gchar *message = NULL;

  if (self->pipeline) {
    int rep_err;

    self->target_state = GST_STATE_NULL;
    gst_element_set_state (self->pipeline, GST_STATE_NULL);

    if (self->runner) {
      ret = gst_validate_runner_printf (self->runner);
      if (rep_err != 0)
        message = "CRITICAL where found";
      else
        message = "No issue found";
    }

    gst_object_unref (self->pipeline);

    if (self->runner)
      g_object_unref (self->runner);
    if (self->monitor)
      g_object_unref (self->monitor);

    if (self->video_sink)
      gst_object_unref (self->video_sink);
    self->pipeline = NULL;
    self->runner = NULL;
    self->monitor = NULL;
    self->video_sink = NULL;
  }

  g_free (self->args);
  self->args = NULL;
  self->target_state = GST_STATE_NULL;

  __fake_exit (self, ret, message);
}

static void
error_cb (GstBus * bus, GstMessage * msg, GstValidateAndroid * self)
{
  GError *err;
  gchar *debug_info;

  gst_message_parse_error (msg, &err, &debug_info);
  set_message (self, "Error received from element %s: %s",
      GST_OBJECT_NAME (msg->src), err->message);
  g_clear_error (&err);
  g_free (debug_info);

  gst_validate_android_clean_pipeline (self);
}

static void
eos_cb (GstBus * bus, GstMessage * msg, GstValidateAndroid * self)
{
  gst_validate_android_clean_pipeline (self);
}

static void
buffering_cb (GstBus * bus, GstMessage * msg, GstValidateAndroid * self)
{
  gint percent;

  if (self->is_live)
    return;

  gst_message_parse_buffering (msg, &percent);
  if (percent < 100 && self->target_state >= GST_STATE_PAUSED) {
    gst_element_set_state (self->pipeline, GST_STATE_PAUSED);
    set_message (self, "Buffering %d%%", percent);
  } else if (self->target_state >= GST_STATE_PLAYING) {
    gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
  } else if (self->target_state >= GST_STATE_PAUSED) {
    set_message (self, "Buffering complete");
  }
}

static void
clock_lost_cb (GstBus * bus, GstMessage * msg, GstValidateAndroid * self)
{
  if (self->target_state >= GST_STATE_PLAYING) {
    gst_element_set_state (self->pipeline, GST_STATE_PAUSED);
    gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
  }
}

static void
request_state_cb (GstBus * bus, GstMessage * msg, GstValidateAndroid * self)
{
  if (GST_IS_VALIDATE_SCENARIO (GST_MESSAGE_SRC (msg))) {
    GST_DEBUG ("Validate requested exit, doing it");
    gst_validate_android_clean_pipeline (self);
  }
}

static void
check_media_size (GstValidateAndroid * self)
{
  GstPad *video_sink_pad;
  GstCaps *caps;
  GstVideoInfo info;

  if (!self->video_sink || !self->app_context.media_size_changed)
    return;

  /* Retrieve the Caps at the entrance of the video sink */
  video_sink_pad = gst_element_get_static_pad (self->video_sink, "sink");
  if (!video_sink_pad)
    return;

  caps = gst_pad_get_current_caps (video_sink_pad);

  if (gst_video_info_from_caps (&info, caps)) {
    info.width = info.width * info.par_n / info.par_d;
    GST_DEBUG ("Media size is %dx%d, notifying application", info.width,
        info.height);

    self->app_context.media_size_changed (info.width, info.height,
        self->app_context.app);
  }

  gst_caps_unref (caps);
  gst_object_unref (video_sink_pad);
}

static void
notify_caps_cb (GObject * object, GParamSpec * pspec, GstValidateAndroid * self)
{
  check_media_size (self);
}

static void
sync_message_cb (GstBus * bus, GstMessage * msg, GstValidateAndroid * self)
{
  if (gst_is_video_overlay_prepare_window_handle_message (msg)) {
    GstElement *element = GST_ELEMENT (GST_MESSAGE_SRC (msg));
    GstPad *sinkpad;

    /* Store video sink for later usage and set window on it if we have one */
    gst_object_replace ((GstObject **) & self->video_sink,
        (GstObject *) element);

    sinkpad = gst_element_get_static_pad (element, "sink");
    if (!sinkpad) {
      sinkpad = gst_element_get_static_pad (element, "video_sink");
    }

    if (sinkpad) {
      g_signal_connect (sinkpad, "notify::caps", (GCallback) notify_caps_cb,
          self);
      gst_object_unref (sinkpad);
    }

    gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (element),
        (guintptr) self->window_handle);
  }
}

/* Notify UI about pipeline state changes */
static void
state_changed_cb (GstBus * bus, GstMessage * msg, GstValidateAndroid * self)
{
  GstState old_state, new_state, pending_state;

  gst_message_parse_state_changed (msg, &old_state, &new_state, &pending_state);
  /* Only pay attention to messages coming from the pipeline, not its children */
  if (GST_MESSAGE_SRC (msg) == GST_OBJECT (self->pipeline)) {
    set_message (self, "State: %s", gst_element_state_get_name (new_state));

    /* The Ready to Paused state change is particularly interesting: */
    if (old_state == GST_STATE_READY && new_state == GST_STATE_PAUSED) {
      /* By now the sink already knows the media size */
      check_media_size (self);
    }
  }
}

/* Check if all conditions are met to report GStreamer as initialized.
 * These conditions will change depending on the application */
static void
check_initialization_complete (GstValidateAndroid * self)
{
  if (!self->initialized && self->window_handle && self->main_loop) {
    GST_DEBUG
        ("Initialization complete, notifying application. window handle: %p",
        (gpointer) self->window_handle);

    if (self->app_context.initialized)
      self->app_context.initialized (self->app_context.app);
    self->initialized = TRUE;
  }
}

static gboolean
parse_debug (const gchar * opt, const gchar * arg, gpointer data, GError ** err)
{

  gst_debug_set_threshold_from_string (arg, FALSE);

  return TRUE;
}

static gboolean
_setup_bus (GstValidateAndroid * self)
{
  GstBus *bus;
  GSource *bus_source;
  gboolean monitor_handles_state;

  bus = gst_element_get_bus (self->pipeline);
  bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func,
      NULL, NULL);
  g_source_attach (bus_source, self->context);
  g_source_unref (bus_source);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback) error_cb,
      self);
  g_signal_connect (G_OBJECT (bus), "message::eos", (GCallback) eos_cb, self);
  g_signal_connect (G_OBJECT (bus), "message::state-changed",
      (GCallback) state_changed_cb, self);
  g_signal_connect (G_OBJECT (bus), "message::buffering",
      (GCallback) buffering_cb, self);
  g_signal_connect (G_OBJECT (bus), "message::clock-lost",
      (GCallback) clock_lost_cb, self);
  g_signal_connect (G_OBJECT (bus), "message::request-state",
      (GCallback) request_state_cb, self);

  gst_bus_enable_sync_message_emission (bus);
  g_signal_connect (G_OBJECT (bus), "sync-message", (GCallback) sync_message_cb,
      self);
  gst_object_unref (bus);

  g_object_get (self->monitor, "handles-states", &monitor_handles_state, NULL);
  if (monitor_handles_state == FALSE) {
    GstStateChangeReturn sret;

    sret = gst_element_set_state (self->pipeline, GST_STATE_PLAYING);
    switch (sret) {
      case GST_STATE_CHANGE_FAILURE:
        /* ignore, we should get an error message posted on the bus */
        g_print ("Pipeline failed to go to PLAYING state\n");
        gst_element_set_state (self->pipeline, GST_STATE_NULL);

        __fake_exit (self, -1, "Pipeline failed to go to PLAYING state");
        return G_SOURCE_REMOVE;
      case GST_STATE_CHANGE_NO_PREROLL:
        g_print ("Pipeline is live.\n");
        self->is_live = TRUE;
        break;
      case GST_STATE_CHANGE_ASYNC:
        g_print ("Prerolling...\r");
        break;
      default:
        break;
    }
    g_print ("Pipeline started\n");
  } else {
    g_print ("Letting scenario handle set state\n");
  }

  return G_SOURCE_REMOVE;
}

static gboolean
_set_validate_parametters (GstValidateAndroid * self, gint argc, gchar ** argv)
{
  gchar *error;
  GOptionEntry entries[] = {
    {"gst-debug", 0, 0, G_OPTION_ARG_CALLBACK, (gpointer) parse_debug,
          "Comma-separated list of category_name:level pairs to set "
          "specific levels for the individual categories. Example: "
          "GST_AUTOPLUG:5,GST_ELEMENT_*:3",
        "LIST"},
    {"debug-no-color", 0, 0, G_OPTION_ARG_NONE, &no_color,
          "disable debug coloration ",
        "LIST"},
    {NULL},
  };

  self->is_launch = TRUE;
  self->pipeline = gst_validate_build_pipeline (argc, argv, &error,
      &self->runner, &self->monitor, entries);

  if (self->pipeline == NULL) {
    if (error) {
      __fake_exit (self, -1, error);

      g_free (error);
    }
    __fake_exit (self, -1, NULL);
  }

  return _setup_bus (self);
}

static gboolean
_set_validate_transcoding_parametters (GstValidateAndroid * self, gint argc,
    gchar ** argv)
{
  gchar *error;
  GOptionEntry entries[] = {
    {"gst-debug", 0, 0, G_OPTION_ARG_CALLBACK, (gpointer) parse_debug,
          "Comma-separated list of category_name:level pairs to set "
          "specific levels for the individual categories. Example: "
          "GST_AUTOPLUG:5,GST_ELEMENT_*:3",
        "LIST"},
    {"debug-no-color", 0, 0, G_OPTION_ARG_NONE, &no_color,
          "disable debug coloration ",
        "LIST"},
    {NULL},
  };

  self->is_transcoder = TRUE;
  self->pipeline = gst_validate_transcoding_build_pipeline (argc, argv,
      &self->runner, &self->monitor, &error, entries);

  if (self->pipeline == NULL) {
    if (error) {
      __fake_exit (self, -1, error);

      g_free (error);
    }
    __fake_exit (self, -1, NULL);
  }

  return _setup_bus (self);
}

static gboolean
_set_parametters (GstValidateAndroid * self)
{
  gint argc;
  gchar **argv, *issue;

  if (!self->args) {
    __fake_exit (self, -1, issue);
    return G_SOURCE_REMOVE;
  }

  argv = g_strsplit (self->args, " ", -1);
  for (argc = 0; argv[argc]; argc++);

  if (g_strcmp0 (argv[0], "validate") == 0)
    return _set_validate_parametters (self, argc, argv);
  if (g_strcmp0 (argv[0], "validate-transcoding") == 0)
    return _set_validate_transcoding_parametters (self, argc, argv);
  if (g_strcmp0 (argv[0], "inspect") == 0) {
    __fake_exit (self, gst_inspect (argc, argv), "");

    return G_SOURCE_REMOVE;
  }

  issue = g_strdup_printf ("Unknown tool: %s", argv[0]);
  __fake_exit (self, -1, issue);

  g_free (issue);
  g_strfreev (argv);
}

static void
_ensure_context (GstValidateAndroid * self)
{
  if (self->context == NULL) {
    self->context = g_main_context_new ();
    g_main_context_push_thread_default (self->context);
  }
}

void
gst_validate_android_set_parametters (GstValidateAndroid * self, gchar * args)
{
  G_LOCK (context_exists);
  self->args = g_strdup (args);
  if (self->context != NULL)
    g_main_context_invoke (self->context, (GSourceFunc) _set_parametters, self);
  G_UNLOCK (context_exists);
}

static gboolean
update_position_cb (GstValidateAndroid * self)
{
  gint64 duration = 0, position = 0;

  if (self->pipeline) {
    if (!gst_element_query_duration (self->pipeline, GST_FORMAT_TIME,
            &duration)) {
      GST_WARNING ("Could not query current duration");

      return TRUE;
    }

    if (!gst_element_query_position (self->pipeline, GST_FORMAT_TIME,
            &position)) {
      GST_WARNING ("Could not query current position");

      return TRUE;
    }

    position = MAX (position, 0);
    duration = MAX (duration, 0);

    set_position (self, "position: %" GST_TIME_FORMAT " / %" GST_TIME_FORMAT,
        GST_TIME_ARGS (position), GST_TIME_ARGS (duration));
  }

  return TRUE;
}

static gpointer
gst_validate_android_main (gpointer user_data)
{
  GSource *timeout_source;
  GstValidateAndroid *self = user_data;

  GST_DEBUG ("GstValidateAndroid main %p", self);

  g_setenv ("GST_VALIDATE_SCENARIOS_PATH",
      "/data/data/org.freedesktop.gstvalidate/scenarios/", TRUE);

  fault_setup ();
  gst_validate_report_add_print_func (priv_validate_print);
  /* Create our own GLib Main Context and make it the default one */

  G_LOCK (context_exists);
  _ensure_context (self);

  self->main_loop = g_main_loop_new (self->context, FALSE);
  if (self->args)
    g_main_context_invoke (self->context, (GSourceFunc) _set_parametters, self);
  G_UNLOCK (context_exists);

  check_initialization_complete (self);

  timeout_source = g_timeout_source_new (250);
  g_source_set_callback (timeout_source, (GSourceFunc) update_position_cb, self,
      NULL);
  g_source_attach (timeout_source, self->context);
  g_source_unref (timeout_source);

  GST_DEBUG ("Starting main loop %p in %p", self->main_loop, self->context);
  g_main_loop_run (self->main_loop);
  GST_DEBUG ("Exited main loop");
  g_main_loop_unref (self->main_loop);
  self->main_loop = NULL;

  /* Free resources */
  g_main_context_pop_thread_default (self->context);
  g_main_context_unref (self->context);
  self->target_state = GST_STATE_NULL;
  if (self->pipeline) {
    gst_element_set_state (self->pipeline, GST_STATE_NULL);
    gst_object_unref (self->pipeline);
    if (self->video_sink)
      gst_object_unref (self->video_sink);
    self->pipeline = NULL;
    self->video_sink = NULL;
  }
  g_free (self->args);

  return NULL;
}

static gpointer
gst_validate_android_init (gpointer user_data)
{
  GST_DEBUG_CATEGORY_INIT (debug_category, "gst-launch-remote", 0,
      "GstValidateAndroid");

  g_set_print_handler (priv_glib_print_handler);
  g_set_printerr_handler (priv_glib_printerr_handler);
  g_log_set_default_handler (priv_glib_log_handler, NULL);

  gst_debug_remove_log_function (gst_debug_log_default);
  gst_debug_remove_log_function_by_data (NULL);
  gst_debug_add_log_function ((GstLogFunction) priv_gst_debug_logcat, NULL,
      NULL);

  start_time = gst_util_get_timestamp ();

  return NULL;
}

GstValidateAndroid *
gst_validate_android_new (const GstValidateAndroidAppContext * ctx)
{
  GstValidateAndroid *self = g_slice_new0 (GstValidateAndroid);
  static GOnce once = G_ONCE_INIT;

  g_once (&once, gst_validate_android_init, NULL);

  self->app_context = *ctx;
  self->thread =
      g_thread_new ("gst-launch-remote", gst_validate_android_main, self);

  return self;
}

void
gst_validate_android_free (GstValidateAndroid * self)
{
  gst_validate_android_clean_pipeline (self);
  g_main_loop_quit (self->main_loop);
  g_thread_join (self->thread);
  g_free (self->args);
  g_free (self->position);
  if (self->message)
    g_string_free (self->message, TRUE);
  g_slice_free (GstValidateAndroid, self);
}

void
gst_validate_android_set_window_handle (GstValidateAndroid * self,
    guintptr handle)
{
  if (!self)
    return;

  GST_DEBUG ("Received window handle %p", (gpointer) handle);

  if (self->window_handle) {
    if (self->window_handle == handle) {
      GST_DEBUG ("New window handle is the same as the previous one");
      if (self->video_sink) {
        gst_video_overlay_expose (GST_VIDEO_OVERLAY (self->video_sink));
      }
      return;
    } else {
      GST_DEBUG ("Released previous window handle %p",
          (gpointer) self->window_handle);
      self->initialized = FALSE;
    }
  }

  self->window_handle = handle;

  if (!self->window_handle) {
    if (self->video_sink) {
      gst_video_overlay_set_window_handle (GST_VIDEO_OVERLAY (self->video_sink),
          (guintptr) NULL);
      gst_element_set_state (self->pipeline, GST_STATE_NULL);
      gst_object_unref (self->pipeline);
      if (self->video_sink)
        gst_object_unref (self->video_sink);
      self->pipeline = NULL;
      self->video_sink = NULL;
    }
  }

  check_initialization_complete (self);
}
