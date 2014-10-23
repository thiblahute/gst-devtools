/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate.c - Validate CLI launch line tool
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

#include <stdlib.h>
#include <string.h>

#include <gio/gio.h>
#include <gst/validate/gst-validate-scenario.h>
#include <gst/validate/gst-validate-utils.h>
#include <gst/validate/media-descriptor-parser.h>


#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include "helpers/gst-validate.h"

static GMainLoop *mainloop;
static gboolean buffering = FALSE;
static gboolean is_live = FALSE;
static GstElement *pipeline;

#ifdef G_OS_UNIX
static gboolean
intr_handler (gpointer user_data)
{
  g_print ("interrupt received.\n");

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate.interupted");

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
    case GST_MESSAGE_ERROR:
    {
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate.error");

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    case GST_MESSAGE_ASYNC_DONE:
      break;
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_MESSAGE_SRC (message) == GST_OBJECT (pipeline)) {
        GstState oldstate, newstate, pending;
        gchar *dump_name;
        gchar *state_transition_name;

        gst_message_parse_state_changed (message, &oldstate, &newstate,
            &pending);

        GST_DEBUG ("State changed (old: %s, new: %s, pending: %s)",
            gst_element_state_get_name (oldstate),
            gst_element_state_get_name (newstate),
            gst_element_state_get_name (pending));

        state_transition_name = g_strdup_printf ("%s_%s",
            gst_element_state_get_name (oldstate),
            gst_element_state_get_name (newstate));
        dump_name = g_strconcat ("ges-launch.", state_transition_name, NULL);


        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
            GST_DEBUG_GRAPH_SHOW_ALL, dump_name);

        g_free (dump_name);
        g_free (state_transition_name);
      }

      break;
    case GST_MESSAGE_WARNING:{
      GError *gerror;
      gchar *debug;
      gchar *name = gst_object_get_path_string (GST_MESSAGE_SRC (message));

      /* dump graph on warning */
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate.warning");

      gst_message_parse_warning (message, &gerror, &debug);
      g_print ("WARNING: from element %s: %s\n", name, gerror->message);
      if (debug)
        g_print ("Additional debug info:\n%s\n", debug);

      g_error_free (gerror);
      g_free (debug);
      g_free (name);
      break;
    }
    case GST_MESSAGE_BUFFERING:{
      gint percent;
      GstBufferingMode mode;

      if (!buffering) {
        g_print ("\n");
      }

      gst_message_parse_buffering (message, &percent);
      gst_message_parse_buffering_stats (message, &mode, NULL, NULL, NULL);
      g_print ("%s %d%%  \r", "Buffering...", percent);

      /* no state management needed for live pipelines */
      if (mode == GST_BUFFERING_LIVE) {
        is_live = TRUE;
        break;
      }

      if (percent == 100) {
        /* a 100% message means buffering is done */
        if (buffering) {
          buffering = FALSE;
          g_print ("Done buffering, setting pipeline to PLAYING\n");
          gst_element_set_state (pipeline, GST_STATE_PLAYING);
        }
      } else {
        /* buffering... */
        if (!buffering) {
          g_print ("Start buffering, setting pipeline to PAUSED\n");
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
        gst_validate_printf (GST_MESSAGE_SRC (message),
            "State change request NULL, " "quiting mainloop\n");
        g_main_loop_quit (mainloop);
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}


int
main (int argc, gchar ** argv)
{
  gint ret = 0;
  int rep_err;
  GstBus *bus;
  gchar *error;
  GstStateChangeReturn sret;
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;
  gboolean monitor_handles_state;
#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif


  pipeline = gst_validate_build_pipeline (argc, argv, &error,
      &runner, &monitor, NULL);
  if (pipeline == NULL) {
    if (error) {
      g_printerr ("%s\n", error);
      g_free (error);

      exit (-1);
    }

    exit (0);
  }
#ifdef G_OS_UNIX
  {
    signal_watch_id =
        g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
  }
#endif

  mainloop = g_main_loop_new (NULL, FALSE);
  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", (GCallback) bus_callback, mainloop);

  g_print ("Starting pipeline\n");
  g_object_get (monitor, "handles-states", &monitor_handles_state, NULL);
  if (monitor_handles_state == FALSE) {
    sret = gst_element_set_state (pipeline, GST_STATE_PLAYING);
    switch (sret) {
      case GST_STATE_CHANGE_FAILURE:
        /* ignore, we should get an error message posted on the bus */
        g_print ("Pipeline failed to go to PLAYING state\n");
        gst_element_set_state (pipeline, GST_STATE_NULL);
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
    g_print ("Pipeline started\n");
  } else {
    g_print ("Letting scenario handle set state\n");
  }

  g_main_loop_run (mainloop);
  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_element_get_state (pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

  /* Clean the bus */
  gst_bus_set_flushing (bus, TRUE);
  gst_object_unref (bus);

  rep_err = gst_validate_runner_printf (runner);
  if (ret == 0) {
    ret = rep_err;
    if (rep_err != 0)
      g_print ("Returning %d as error where found", rep_err);
  }

exit:
  g_main_loop_unref (mainloop);
  g_object_unref (pipeline);
  g_object_unref (runner);
  g_object_unref (monitor);
  gst_validate_deinit ();
#ifdef G_OS_UNIX
  g_source_remove (signal_watch_id);
#endif

  g_print ("\n=======> Test %s (Return value: %i)\n\n",
      ret == 0 ? "PASSED" : "FAILED", ret);
  return ret;
}
