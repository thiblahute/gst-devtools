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

#include <gst/gst.h>
#include <gio/gio.h>
#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-scenario.h>
#include <gst/validate/gst-validate-utils.h>
#include <gst/validate/media-descriptor-parser.h>
#include <gst/validate/gst-validate-bin-monitor.h>
#include <gst/validate/gst-validate-pipeline-monitor.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif
#include <locale.h>             /* for LC_ALL */

static gint ret = 0;
static GMainLoop *mainloop;
static GstElement *pipeline;
static gboolean buffering = FALSE;

static gboolean is_live = FALSE;

#ifdef G_OS_UNIX
static gboolean
intr_handler (gpointer user_data)
{
  g_print ("interrupt received.\n");

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate.interrupted");

  g_main_loop_quit (mainloop);

  ret = SIGINT;

  /* Keep signal handler, it will be removed later anyway */
  return TRUE;
}
#endif /* G_OS_UNIX */

typedef struct
{
  GMainLoop *mainloop;
  GstValidateMonitor *monitor;
} BusCallbackData;

static gboolean
bus_callback (GstBus * bus, GstMessage * message, gpointer data)
{
  BusCallbackData *bus_callback_data = data;
  GMainLoop *loop = bus_callback_data->mainloop;
  GstValidateMonitor *monitor = bus_callback_data->monitor;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate.error");

      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      if (!g_getenv ("GST_VALIDATE_SCENARIO"))
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

      g_clear_error (&gerror);
      g_free (debug);
      g_free (name);
      break;
    }
    case GST_MESSAGE_BUFFERING:{
      gint percent;
      GstBufferingMode mode;
      GstState target_state = GST_STATE_PLAYING;
      gboolean monitor_handles_state;

      g_object_get (monitor, "handles-states", &monitor_handles_state, NULL);
      if (monitor_handles_state && GST_IS_VALIDATE_BIN_MONITOR (monitor)) {
        target_state =
            gst_validate_scenario_get_target_state (GST_VALIDATE_BIN_MONITOR
            (monitor)->scenario);
      }

      if (!buffering) {
        g_print ("\n");
      }

      gst_message_parse_buffering (message, &percent);
      gst_message_parse_buffering_stats (message, &mode, NULL, NULL, NULL);

      /* no state management needed for live pipelines */
      if (mode == GST_BUFFERING_LIVE) {
        is_live = TRUE;
        break;
      }

      if (percent == 100) {
        /* a 100% message means buffering is done */
        if (buffering) {
          buffering = FALSE;

          if (target_state == GST_STATE_PLAYING) {
            gst_element_set_state (pipeline, GST_STATE_PLAYING);
          }
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
        gst_validate_printf (GST_MESSAGE_SRC (message),
            "State change request NULL, quiting mainloop\n");
        g_main_loop_quit (mainloop);
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}

static gboolean
_is_playbin_pipeline (int argc, gchar ** argv)
{
  gint i;

  for (i = 0; i < argc; i++) {
    if (g_ascii_strncasecmp (argv[i], "playbin", 7) == 0) {
      return TRUE;
    }
  }

  return FALSE;
}

static gboolean
_execute_set_subtitles (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  gchar *uri, *fname;
  GFile *tmpfile, *folder;
  const gchar *subtitle_file, *subtitle_dir;

  subtitle_file = gst_structure_get_string (action->structure, "subtitle-file");
  g_return_val_if_fail (subtitle_file != NULL, FALSE);
  subtitle_dir = gst_structure_get_string (action->structure, "subtitle-dir");

  g_object_get (scenario->pipeline, "current-uri", &uri, NULL);
  tmpfile = g_file_new_for_uri (uri);
  g_free (uri);

  folder = g_file_get_parent (tmpfile);

  fname = g_strdup_printf ("%s%s%s%s",
      subtitle_dir ? subtitle_dir : "",
      subtitle_dir ? G_DIR_SEPARATOR_S : "",
      g_file_get_basename (tmpfile), subtitle_file);
  gst_object_unref (tmpfile);

  tmpfile = g_file_get_child (folder, fname);
  g_free (fname);
  gst_object_unref (folder);

  uri = g_file_get_uri (tmpfile);
  gst_validate_printf (action, "Setting subtitle file to: %s", uri);
  g_object_set (scenario->pipeline, "suburi", uri, NULL);
  g_free (uri);

  return TRUE;
}

static void
_register_playbin_actions (void)
{
/* *INDENT-OFF* */
  gst_validate_register_action_type ("set-subtitle", "validate-launcher", _execute_set_subtitles,
      (GstValidateActionParameter []) {
        {
          .name = "subtitle-file",
          .description = "Sets a subtitles file on a playbin pipeline",
          .mandatory = TRUE,
          .types = "string (A URI)",
          NULL
        },
        {NULL}
      },
      "Action to set a subtitle file to use on a playbin pipeline.\n"
      "The subtitles file that will be used should be specified\n"
      "relative to the playbin URI in use thanks to the subtitle-file\n"
      "action property. You can also specify a folder with subtitle-dir\n"
      "For example if playbin.uri='file://some/uri.mov'\n"
      "and action looks like 'set-subtitle, subtitle-file=en.srt'\n"
      "the subtitle URI will be set to 'file:///some/uri.mov.en.srt'\n",
      FALSE);
/* *INDENT-ON* */
}

int
main (int argc, gchar ** argv)
{
  GError *err = NULL;
  gchar *scenario = NULL, *configs = NULL, *media_info = NULL;
  gboolean list_scenarios = FALSE, monitor_handles_state,
      inspect_action_type = FALSE;
  GstStateChangeReturn sret;
  gchar *output_file = NULL;
  BusCallbackData bus_callback_data = { 0, };

#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif
  int rep_err;

  GOptionEntry options[] = {
    {"set-scenario", '\0', 0, G_OPTION_ARG_FILENAME, &scenario,
        "Let you set a scenario, it can be a full path to a scenario file"
          " or the name of the scenario (name of the file without the"
          " '.scenario' extension).", NULL},
    {"list-scenarios", 'l', 0, G_OPTION_ARG_NONE, &list_scenarios,
        "List the available scenarios that can be run", NULL},
    {"scenarios-defs-output-file", '\0', 0, G_OPTION_ARG_FILENAME,
          &output_file, "The output file to store scenarios details. "
          "Implies --list-scenario",
        NULL},
    {"inspect-action-type", 't', 0, G_OPTION_ARG_NONE, &inspect_action_type,
          "Inspect the available action types with which to write scenarios."
          " Specify an action type if you want its full description."
          " If no action type is given the full list of available ones gets printed.",
        NULL},
    {"set-media-info", '\0', 0, G_OPTION_ARG_FILENAME, &media_info,
          "Set a media_info XML file descriptor to share information about the"
          " media file that will be reproduced.",
        NULL},
    {"set-configs", '\0', 0, G_OPTION_ARG_STRING, &configs,
          "Select a config scenario (one including 'is-config=true' in its"
          " description). Specify multiple ones using ':' as separator."
          " This option overrides the GST_VALIDATE_SCENARIO environment variable.",
        NULL},
    {NULL}
  };
  GOptionContext *ctx;
  gchar **argvn;
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;
  GstBus *bus;

  setlocale (LC_ALL, "");

  g_set_prgname ("gst-validate-" GST_API_VERSION);
  ctx = g_option_context_new ("PIPELINE-DESCRIPTION");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_set_summary (ctx, "Runs a gst launch pipeline, adding "
      "monitors to it to identify issues in the used elements. At the end"
      " a report will be printed. To view issues as they are created, set"
      " the env var GST_DEBUG=validate:2 and it will be printed "
      "as gstreamer debugging");

  if (argc == 1) {
    g_print ("%s", g_option_context_get_help (ctx, FALSE, NULL));
    exit (1);
  }

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    g_clear_error (&err);
    exit (1);
  }

  if (scenario || configs) {
    gchar *scenarios;

    if (scenario)
      scenarios = g_strjoin (":", scenario, configs, NULL);
    else
      scenarios = g_strdup (configs);

    g_setenv ("GST_VALIDATE_SCENARIO", scenarios, TRUE);
    g_free (scenarios);
    g_free (scenario);
    g_free (configs);
  }

  gst_init (&argc, &argv);
  gst_validate_init ();

  if (list_scenarios || output_file) {
    if (gst_validate_list_scenarios (argv + 1, argc - 1, output_file))
      return 1;
    return 0;
  }

  if (inspect_action_type) {
    _register_playbin_actions ();

    if (!gst_validate_print_action_types ((const gchar **) argv + 1, argc - 1)) {
      GST_ERROR ("Could not print all wanted types");
      return -1;
    }

    return 0;
  }

  if (argc == 1) {
    g_print ("%s", g_option_context_get_help (ctx, FALSE, NULL));
    g_option_context_free (ctx);
    exit (1);
  }

  g_option_context_free (ctx);

  runner = gst_validate_runner_new ();
  if (!runner) {
    g_printerr ("Failed to setup Validate Runner\n");
    exit (1);
  }

  /* Create the pipeline */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
  pipeline = (GstElement *) gst_parse_launchv ((const gchar **) argvn, &err);
  g_free (argvn);
  if (!pipeline) {
    g_print ("Failed to create pipeline: %s\n",
        err ? err->message : "unknown reason");
    g_clear_error (&err);
    g_object_unref (runner);

    exit (1);
  }
  if (!GST_IS_PIPELINE (pipeline)) {
    GstElement *new_pipeline = gst_pipeline_new ("");

    gst_bin_add (GST_BIN (new_pipeline), pipeline);
    pipeline = new_pipeline;
  }

  gst_pipeline_set_auto_flush_bus (GST_PIPELINE (pipeline), FALSE);
#ifdef G_OS_UNIX
  signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
#endif

  if (_is_playbin_pipeline (argc - 1, argv + 1)) {
    _register_playbin_actions ();
  }

  monitor = gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline),
      runner, NULL);
  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (monitor));

  if (media_info) {
    GError *err = NULL;
    GstValidateMediaDescriptorParser *parser =
        gst_validate_media_descriptor_parser_new (runner,
        media_info, &err);

    if (parser == NULL) {
      GST_ERROR ("Could not use %s as a media-info file (error: %s)",
          media_info, err ? err->message : "Unknown error");

      g_free (media_info);
      exit (1);
    }

    gst_validate_monitor_set_media_descriptor (monitor,
        GST_VALIDATE_MEDIA_DESCRIPTOR (parser));
    gst_object_unref (parser);
    g_free (media_info);
  }

  mainloop = g_main_loop_new (NULL, FALSE);
  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  bus_callback_data.mainloop = mainloop;
  bus_callback_data.monitor = monitor;
  g_signal_connect (bus, "message", (GCallback) bus_callback,
      &bus_callback_data);

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
  gst_bus_remove_signal_watch (bus);
  gst_object_unref (bus);

  rep_err = gst_validate_runner_exit (runner, TRUE);
  if (ret == 0) {
    ret = rep_err;
    if (rep_err != 0)
      g_print ("Returning %d as errors were found\n", rep_err);
  }

exit:
  g_main_loop_unref (mainloop);
  g_object_unref (pipeline);
  g_object_unref (runner);
  gst_validate_reporter_purge_reports (GST_VALIDATE_REPORTER (monitor));
  g_object_unref (monitor);
  g_clear_error (&err);
#ifdef G_OS_UNIX
  g_source_remove (signal_watch_id);
#endif

  g_print ("\n=======> Test %s (Return value: %i)\n\n",
      ret == 0 ? "PASSED" : "FAILED", ret);

  gst_validate_deinit ();
  gst_deinit ();
  return ret;
}
