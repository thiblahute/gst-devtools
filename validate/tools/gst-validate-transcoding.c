/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-validate-transcoding.c - CLI tool to validate transcoding operations
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gst-validate-transcoding.h"

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/video/video.h>
#include <gst/pbutils/encoding-profile.h>


#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include <gst/validate/gst-validate-scenario.h>

static gint ret = 0;
static gboolean buffering = FALSE;
static gboolean is_live = FALSE;
static GstElement *pipeline = NULL;

static GMainLoop *mainloop;
static gboolean eos_on_shutdown = FALSE;

#ifdef G_OS_UNIX
static gboolean
intr_handler (gpointer user_data)
{
  g_print ("interrupt received.\n");

  if (eos_on_shutdown) {
    g_print ("Sending EOS to the pipeline\n");
    eos_on_shutdown = FALSE;
    gst_element_send_event (GST_ELEMENT_CAST (user_data), gst_event_new_eos ());
    return TRUE;
  }
  g_main_loop_quit (mainloop);

  /* remove signal handler */
  return FALSE;
}
#endif /* G_OS_UNIX */

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  GMainLoop *loop = data;
  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_STATE_CHANGED:
    {
      if (GST_MESSAGE_SRC (message) == GST_OBJECT_CAST (pipeline)) {
        gchar *dotname;
        GstState old, new, pending;

        gst_message_parse_state_changed (message, &old, &new, &pending);

        if (new == GST_STATE_PLAYING) {
          GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
              GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate-transcode.playing");
        }

        dotname = g_strdup_printf ("gst-validate-transcoding.%s_%s",
            gst_element_state_get_name (old), gst_element_state_get_name (new));

        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
            GST_DEBUG_GRAPH_SHOW_ALL, dotname);
        g_free (dotname);
      }
      break;
    }
    case GST_MESSAGE_ERROR:
    {
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_BUFFERING:{
      gint percent;

      if (!buffering) {
        g_print ("\n");
      }

      gst_message_parse_buffering (message, &percent);
      g_print ("%s %d%%  \r", "Buffering...", percent);

      /* no state management needed for live pipelines */
      if (is_live)
        break;

      if (percent == 100) {
        /* a 100% message means buffering is done */
        if (buffering) {
          buffering = FALSE;
          gst_element_set_state (pipeline, GST_STATE_PLAYING);
        }
      } else {
        /* buffering... */
        if (!buffering) {
          gst_element_set_state (pipeline, GST_STATE_PAUSED);
          buffering = TRUE;
        }
      }
      break;
    }
    case GST_MESSAGE_REQUEST_STATE:
    {
      GstState state;

      gst_message_parse_request_state (message, &state);

      if (GST_IS_VALIDATE_SCENARIO (GST_MESSAGE_SRC (message))
          && state == GST_STATE_NULL) {
        GST_VALIDATE_REPORT (GST_MESSAGE_SRC (message),
            SCENARIO_ACTION_EXECUTION_ISSUE,
            "Force stopping a transcoding pipeline is not recommanded"
            " you should make sure to finalize it using a EOS event");

        gst_validate_printf (pipeline, "State change request NULL, "
            "quiting mainloop\n");
        g_main_loop_quit (mainloop);
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}

gint
main (int argc, gchar ** argv)
{
  GstBus *bus;
  int rep_err;
  gchar *error;
  GstStateChangeReturn sret;
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;

#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif

  pipeline = gst_validate_transcoding_build_pipeline (argc, argv,
      &runner, &monitor, &error, NULL);

  if (pipeline == NULL) {
    if (error) {
      g_printerr ("%s\n", error);
      g_free (error);

      exit (-1);
    }

    exit (0);
  }
#ifdef G_OS_UNIX
  signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
#endif

  mainloop = g_main_loop_new (NULL, FALSE);
  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", (GCallback) bus_callback, mainloop);
  gst_object_unref (bus);

  g_print ("Starting pipeline\n");
  sret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
  switch (sret) {
    case GST_STATE_CHANGE_FAILURE:
      /* ignore, we should get an error message posted on the bus */
      g_print ("Pipeline failed to go to PLAYING state\n");
      ret = -1;
      goto exit;
    case GST_STATE_CHANGE_NO_PREROLL:
      g_print ("Pipeline is live.\n");
      is_live = TRUE;
      break;
    case GST_STATE_CHANGE_ASYNC:
      g_print ("Prerolling...\r");
      break;
    default:
      break;
  }

  g_main_loop_run (mainloop);

  rep_err = gst_validate_runner_printf (runner);
  if (ret == 0)
    ret = rep_err;

exit:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_main_loop_unref (mainloop);
  g_object_unref (pipeline);
  g_object_unref (monitor);
  g_object_unref (runner);
  gst_validate_deinit ();

#ifdef G_OS_UNIX
  g_source_remove (signal_watch_id);
#endif

  g_print ("\n=======> Test %s (Return value: %i)\n\n",
      ret == 0 ? "PASSED" : "FAILED", ret);
  return ret;
}
