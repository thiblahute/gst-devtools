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

#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif


/*
Usage examples:
-------------

1- 3 testsrc, 1 after the other, and the otherone in the backround 
gnl-validate-1.0 --set-sink autovideosink \
 --set-scenario=scrub_forward_seeking \
 gnlsource, bin_desc=\"videotestsrc pattern=snow\",start=0.0,duration=1000000000\; \
 gnlsource, bin_desc=\"videotestsrc\",start=1000000000,duration=1000000000\; \
 gnlsource, bin_desc=\"videotestsrc pattern=ball\",start=1000000000,duration=2000000000,priority=2\;

2- Simple example with 2 sources and one compositor
gnl-validate-1.0 --set-sink autovideosink \
 --set-scenario=scrub_forward_seeking \
 gnlsource, bin_desc=\"videotestsrc pattern=snow\",start=0.0,duration=1000000000,priority=1\; \
 gnlsource, bin_desc=\"videotestsrc\",start=1000000000,duration=1000000000,priority=2\; \
 gnloperation, bin_desc=compositor,start=0,duration=2000000000,priority=0\;

*/

static GMainLoop *mainloop;
static GstElement *pipeline;
static gboolean buffering = FALSE;

static gboolean is_live = FALSE;

#ifdef G_OS_UNIX
static gboolean
intr_handler (gpointer user_data)
{
  g_print ("interrupt received.\n");

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

static gchar *
_gst_parse_escape (const gchar * str)
{
  GString *gstr = NULL;
  gboolean in_quotes;

  g_return_val_if_fail (str != NULL, NULL);

  gstr = g_string_sized_new (strlen (str));

  in_quotes = FALSE;

  while (*str) {
    if (*str == '"' && (!in_quotes || (in_quotes && *(str - 1) != '\\')))
      in_quotes = !in_quotes;

    if (*str == ' ' && !in_quotes)
      g_string_append_c (gstr, '\\');

    g_string_append_c (gstr, *str);
    str++;
  }

  return g_string_free (gstr, FALSE);
}

static gboolean
_is_clock_time (GParamSpec * pspec)
{
  if (G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_UINT64)
    return TRUE;
  else if (G_PARAM_SPEC_VALUE_TYPE (pspec) == G_TYPE_INT64 &&
      (((GParamSpecInt64 *) pspec)->minimum == 0 &&
          ((GParamSpecInt64 *) pspec)->maximum == G_MAXINT64))
    return TRUE;

  return FALSE;
}

static gboolean
_set_property (GQuark field_id, const GValue * value, GObject * object)
{
  GParamSpec *pspec = g_object_class_find_property (G_OBJECT_GET_CLASS (object),
      g_quark_to_string (field_id));

  if (_is_clock_time (pspec) && G_VALUE_HOLDS (value, G_TYPE_DOUBLE)) {
    GValue rvalue = { 0, };
    gdouble val = g_value_get_double (value);

    g_value_init (&rvalue, G_TYPE_UINT64);
    if (val == -1.0)
      g_value_set_uint64 (&rvalue, GST_CLOCK_TIME_NONE);
    else
      g_value_set_uint64 (&rvalue, val * GST_SECOND);

    value = &rvalue;
  }

  g_object_set_property (object, g_quark_to_string (field_id), value);

  return TRUE;
}

static GstElement *
_parse_composition (gchar ** argv, GError ** error)
{
  gint i;
  GString *str;
  gchar *tmp;
  gboolean unused;
  gchar **argvp, *arg;
  GstCaps *compo_desc;

  GstElement *comp = gst_element_factory_make ("gnlcomposition", NULL);

  g_return_val_if_fail (comp != NULL, NULL);
  g_return_val_if_fail (argv != NULL, NULL);
  g_return_val_if_fail (error == NULL || *error == NULL, NULL);

  /* let's give it a nice size. */
  str = g_string_sized_new (1024);

  argvp = argv;
  while (*argvp) {
    arg = *argvp;
    GST_DEBUG ("escaping argument %s", arg);
    tmp = _gst_parse_escape (arg);
    g_string_append (str, tmp);
    g_free (tmp);
    g_string_append_c (str, ' ');
    argvp++;
  }

  compo_desc = gst_caps_from_string (str->str);
  GST_ERROR ("Desc is %s", str->str);
  g_string_free (str, TRUE);

  g_return_val_if_fail (compo_desc, NULL);

  for (i = 0; i < gst_caps_get_size (compo_desc); i++) {
    GstElement *element;
    const gchar *bin_desc;
    GstStructure *s = gst_caps_get_structure (compo_desc, i);
    const gchar *name = gst_structure_get_name (s);

    if (g_strcmp0 (name, "properties") == 0) {
      gst_structure_foreach (s, (GstStructureForeachFunc) _set_property, comp);

      continue;
    }

    element = gst_element_factory_make (name, NULL);
    if (!element) {
      GST_ERROR ("Could not create gnl element: %s", name);

      goto failed;
    }

    bin_desc = gst_structure_get_string (s, "bin_desc");
    if (bin_desc) {
      GstElement *child;

      child = gst_element_factory_make (bin_desc, NULL);
      if (!child)
        child = gst_parse_bin_from_description (bin_desc, TRUE, error);

      if (*error) {

        GST_ERROR ("Error initializing: %s\n", (*error)->message);
        gst_object_unref (element);

        goto failed;
      }

      if (!gst_bin_add (GST_BIN (element), child)) {
        GST_ERROR_OBJECT (element, "Could not add %" GST_PTR_FORMAT, child);

        gst_object_unref (element);
        gst_object_unref (child);

        goto failed;
      }

      gst_structure_remove_field (s, "bin_desc");
    }

    gst_structure_foreach (s, (GstStructureForeachFunc) _set_property, element);

    GST_ERROR_OBJECT (element, "Adding %" GST_PTR_FORMAT, element);
    g_signal_emit_by_name (comp, "add-object", element, &unused);
  }


  return comp;

failed:
  gst_object_unref (comp);

  if (compo_desc) {
    gst_caps_unref (compo_desc);
  }

  return NULL;
}

int
main (int argc, gchar ** argv)
{
  GError *err = NULL;
  const gchar *scenario = NULL, *configs = NULL;
  gboolean list_scenarios = FALSE, monitor_handles_state;
  GstStateChangeReturn sret;
  gchar *output_file = NULL;
  gint ret = 0, commit;
  const gchar *sink_str = "fakesink";
  GstElement *comp, *sink, *queue;

#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif
  int rep_err;

  GOptionEntry options[] = {
    {"set-scenario", '\0', 0, G_OPTION_ARG_STRING, &scenario,
        "Let you set a scenario, it will override the GST_VALIDATE_SCENARIO "
          "environment variable", NULL},
    {"list-scenarios", 'l', 0, G_OPTION_ARG_NONE, &list_scenarios,
        "List the avalaible scenarios that can be run", NULL},
    {"scenarios-defs-output-file", '\0', 0, G_OPTION_ARG_FILENAME,
          &output_file, "The output file to store scenarios details. "
          "Implies --list-scenario",
        NULL},
    {"set-configs", '\0', 0, G_OPTION_ARG_STRING, &configs,
          "Let you set a config scenario, the scenario needs to be set as 'config"
          "' you can specify a list of scenario separated by ':'"
          " it will override the GST_VALIDATE_SCENARIO environment variable,",
        NULL},
    {"set-sink", '\0', 0, G_OPTION_ARG_STRING, &sink_str,
          "Sets the sink element factory name to use",
        NULL},
    {NULL}
  };
  GOptionContext *ctx;
  gchar **argvn;
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;
  GstBus *bus;

  g_set_prgname ("gnl-validate-" GST_API_VERSION);
  ctx = g_option_context_new ("gnonlin validate");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_set_summary (ctx,
      "Runs a gnlcomposition based pipeline, adding "
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
    exit (1);
  }

  if (scenario || configs) {
    gchar *scenarios;

    if (scenario)
      scenarios = g_strjoin (":", scenario, configs, NULL);
    else
      scenarios = g_strdup (configs);

    GST_ERROR ("====> SCENRIO: %s", scenarios);
    g_setenv ("GST_VALIDATE_SCENARIO", scenarios, TRUE);
    g_free (scenarios);
  }

  gst_init (&argc, &argv);
  gst_validate_init ();

  if (list_scenarios || output_file) {
    if (gst_validate_list_scenarios (argv + 1, argc - 1, output_file))
      return 1;
    return 0;
  }

  if (argc == 1) {
    g_print ("%s", g_option_context_get_help (ctx, FALSE, NULL));
    g_option_context_free (ctx);
    exit (1);
  }

  g_option_context_free (ctx);

  /* Create the pipeline */
  GST_ERROR ("======> START!");
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
  comp = _parse_composition (argvn, &err);

  if (!comp) {
    g_print ("Failed to create pipeline: %s\n",
        err ? err->message : "unknown reason");
    exit (1);
  }

  g_free (argvn);

  pipeline = gst_pipeline_new ("gnl-pipeline");
  gst_bin_add (GST_BIN (pipeline), comp);

  sink = gst_parse_bin_from_description (sink_str, TRUE, &err);
  if (!sink) {
    g_print ("Failed to create sink: %s\n", sink_str);
    exit (1);
  }

  queue = gst_element_factory_make ("queue", NULL);

  gst_bin_add_many (GST_BIN (pipeline), queue, sink, NULL);
  g_assert (gst_element_link_many (comp, queue, sink, NULL));


#ifdef G_OS_UNIX
  signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
#endif

  runner = gst_validate_runner_new ();
  if (!runner) {
    g_printerr ("Failed to setup Validate Runner\n");
    exit (1);
  }

  monitor = gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline),
      runner, NULL);
  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (monitor));

  mainloop = g_main_loop_new (NULL, FALSE);
  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", (GCallback) bus_callback, mainloop);
  gst_object_unref (bus);

  g_print ("Starting pipeline\n");
  g_object_get (monitor, "handles-states", &monitor_handles_state, NULL);
  g_signal_emit_by_name (comp, "commit", TRUE, &commit);
  if (monitor_handles_state == FALSE) {
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
    g_print ("Pipeline started\n");
  } else {
    g_print ("Letting scenario handle set state\n");
  }

  g_main_loop_run (mainloop);

  rep_err = gst_validate_runner_printf (runner);
  if (ret == 0) {
    ret = rep_err;
    if (rep_err != 0)
      g_print ("Returning %d as error where found", rep_err);
  }

exit:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_main_loop_unref (mainloop);
  g_object_unref (pipeline);
  g_object_unref (runner);
  g_object_unref (monitor);
#ifdef G_OS_UNIX
  g_source_remove (signal_watch_id);
#endif

  g_print ("\n=======> Test %s (Return value: %i)\n\n",
      ret == 0 ? "PASSED" : "FAILED", ret);
  return ret;
}
