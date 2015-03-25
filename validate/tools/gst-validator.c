/* GstValidate
 * Copyright (C) 2015 Thibault Saunier <tsaunier@gnome.org>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <glib.h>
#include <glib/gprintf.h>
#include <stdlib.h>
#include <string.h>
#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include "gst-validator.h"

#include <gst/validate/validate.h>
#include <gst/validate/gst-validate-scenario.h>
#include <gst/validate/gst-validate-utils.h>
#include <gst/validate/media-descriptor-parser.h>

struct _GstValidatorPrivate
{
#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif

  gint exit_code;
  GMainLoop *mainloop;
  gboolean buffering;
  gboolean is_live;
  GstElement *pipeline;
  gchar *media_info;

  GstBus *bus;
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;
};

enum
{
  CREATE_PIPELINE,
  REGISTER_EXTRA_ACTION_TYPES,
  LAST_SIGNAL
};

static guint _signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (GstValidator, gst_validator, G_TYPE_APPLICATION);

#ifdef G_OS_UNIX
static gboolean
intr_handler (GstValidator * self)
{
  g_print ("interrupt received.\n");

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (self->priv->pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "gst-launch.interupted");

  g_application_release (G_APPLICATION (self));

  /* remove signal handler */
  return TRUE;
}
#endif /* G_OS_UNIX */

static gboolean
bus_callback (GstBus * bus, GstMessage * message, GstValidator * self)
{
  GstValidatorPrivate *priv = self->priv;

  switch (GST_MESSAGE_TYPE (message)) {
    case GST_MESSAGE_ERROR:
    {
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (priv->pipeline),
          GST_DEBUG_GRAPH_SHOW_ALL, "gst-validate.error");

      GST_ERROR ("===> ERROR !");
      g_application_release (G_APPLICATION (self));
      break;
    }
    case GST_MESSAGE_EOS:
      g_printerr ("\nDone\n");
      g_application_release (G_APPLICATION (self));
      break;
    case GST_MESSAGE_ASYNC_DONE:
      break;
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_MESSAGE_SRC (message) == GST_OBJECT (priv->pipeline)) {
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


        GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (priv->pipeline),
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
      GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (priv->pipeline),
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

      if (!priv->buffering) {
        g_print ("\n");
      }

      gst_message_parse_buffering (message, &percent);
      gst_message_parse_buffering_stats (message, &mode, NULL, NULL, NULL);
      g_print ("%s %d%%  \r", "Buffering...", percent);

      /* no state management needed for live pipelines */
      if (mode == GST_BUFFERING_LIVE) {
        priv->is_live = TRUE;
        break;
      }

      if (percent == 100) {
        /* a 100% message means buffering is done */
        if (priv->buffering) {
          priv->buffering = FALSE;
          g_print ("Done buffering, setting pipeline to PLAYING\n");
          gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
        }
      } else {
        /* buffering... */
        if (!priv->buffering) {
          g_print ("Start buffering, setting pipeline to PAUSED\n");
          gst_element_set_state (priv->pipeline, GST_STATE_PAUSED);
          priv->buffering = TRUE;
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

        g_application_release (G_APPLICATION (self));
      }
      break;
    }
    default:
      break;
  }

  return TRUE;
}


static gboolean
_setup_validate_runner (GstValidator * self)
{
  gboolean registered = TRUE;
  GstValidatorPrivate *priv = self->priv;

  g_signal_emit (self, _signals[REGISTER_EXTRA_ACTION_TYPES], 0, priv->pipeline,
      &registered);

  if (!registered) {
    g_printerr ("Could not register extra action types");

    return FALSE;
  }

  priv->runner = gst_validate_runner_new ();
  if (!priv->runner) {
    g_printerr ("Failed to setup Validate Runner\n");

    return FALSE;
  }

  priv->monitor =
      gst_validate_monitor_factory_create (GST_OBJECT_CAST (priv->pipeline),
      priv->runner, NULL);
  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER
      (priv->monitor));

  if (priv->media_info) {
    GError *err = NULL;
    GstMediaDescriptorParser *parser =
        gst_media_descriptor_parser_new (priv->runner,
        priv->media_info, &err);

    if (parser == NULL) {
      GST_ERROR ("Could not use %s as a media-info file (error: %s)",
          priv->media_info, err ? err->message : "Unknown error");

      return FALSE;
    }

    gst_validate_monitor_set_media_descriptor (priv->monitor,
        GST_MEDIA_DESCRIPTOR (parser));
  }

  return TRUE;
}

static gboolean
_local_command_line (GApplication * application, gchar ** arguments[],
    gint * exit_status)
{
  GstValidator *self = GST_VALIDATOR (application);
  GstValidatorPrivate *priv = self->priv;
  GError *error = NULL;
  gchar **argv;
  gint argc;
  GOptionContext *ctx;

  gchar *scenario = NULL, *configs = NULL, *output_file = NULL;
  gboolean list_scenarios = FALSE;
  gboolean inspect_action_type = FALSE;

  GOptionEntry options[] = {
    {"set-scenario", '\0', 0, G_OPTION_ARG_STRING, &scenario,
        "Let you set a scenario, it can be a full path to a scenario file"
          " or the name of the scenario (name of the file without the"
          " '.scenario' extension).", NULL},
    {"list-scenarios", 'l', 0, G_OPTION_ARG_NONE, &list_scenarios,
        "List the avalaible scenarios that can be run", NULL},
    {"scenarios-defs-output-file", '\0', 0, G_OPTION_ARG_FILENAME,
          &output_file, "The output file to store scenarios details. "
          "Implies --list-scenario",
        NULL},
    {"inspect-action-type", 't', 0, G_OPTION_ARG_NONE, &inspect_action_type,
          "Inspect the avalaible action types with which to write scenarios"
          " if no parameter passed, it will list all avalaible action types"
          " otherwize will print the full description of the wanted types",
        NULL},
    {"set-media-info", '\0', 0, G_OPTION_ARG_STRING, &priv->media_info,
          "Set a media_info XML file descriptor to share information about the"
          " media file that will be reproduced.",
        NULL},
    {"set-configs", '\0', 0, G_OPTION_ARG_STRING, &configs,
          "Let you set a config scenario, the scenario needs to be set as 'config"
          "' you can specify a list of scenario separated by ':'"
          " it will override the GST_VALIDATE_SCENARIO environment variable.",
        NULL},
    {NULL}
  };

  ctx = g_option_context_new ("PIPELINE-DESCRIPTION");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_set_summary (ctx, "Runs a gst launch pipeline, adding "
      "monitors to it to identify issues in the used elements. At the end"
      " a report will be printed. To view issues as they are created, set"
      " the env var GST_DEBUG=validate:2 and it will be printed "
      "as gstreamer debugging");

  argv = *arguments;
  argc = g_strv_length (argv);
  *exit_status = 0;

  if (argc == 1) {
    g_print ("%s", g_option_context_get_help (ctx, FALSE, NULL));
    *exit_status = 1;

    return FALSE;
  }

  if (!g_option_context_parse (ctx, &argc, &argv, &error)) {
    g_printerr ("Error initializing: %s\n", error->message);
    g_option_context_free (ctx);
    *exit_status = 1;
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
    if (gst_validate_list_scenarios (argv + 1, argc - 1, output_file)) {
      *exit_status = 1;

      return FALSE;
    }
    return TRUE;
  }

  if (inspect_action_type) {
    gboolean registered = TRUE;

    g_signal_emit (self, _signals[REGISTER_EXTRA_ACTION_TYPES], 0,
        priv->pipeline, &registered);

    if (!registered
        || !gst_validate_print_action_types ((const gchar **) argv + 1,
            argc - 1)) {

      GST_ERROR ("Could not print all wanted types");
      *exit_status = 1;
      return FALSE;
    }

    return TRUE;
  }

  g_signal_emit (self, _signals[CREATE_PIPELINE], 0, argc, argv, &error,
      &priv->pipeline);

  GST_ERROR ("Returned Pipeline %p", priv->pipeline);

  if (!priv->pipeline) {
    *exit_status = 1;

    g_print ("Failed to create pipeline: \n");

    return FALSE;
  }

  if (!_setup_validate_runner (self)) {
    *exit_status = 1;

    g_print ("Could not setup the validate runner");

    return FALSE;
  }

  if (!g_application_register (application, NULL, &error)) {
    *exit_status = 1;
    g_error_free (error);

    return FALSE;
  }

  return TRUE;
}

static gboolean
_launch_pipeline (GstValidator * self)
{
  gboolean monitor_handles_state;
  GstValidatorPrivate *priv = self->priv;

  if (!priv->pipeline)
    return FALSE;

  priv->bus = gst_element_get_bus (priv->pipeline);

  gst_bus_add_signal_watch (priv->bus);
  g_signal_connect (priv->bus, "message", (GCallback) bus_callback, self);

  g_print ("Starting pipeline\n");
  g_object_get (priv->monitor, "handles-states", &monitor_handles_state, NULL);

  if (monitor_handles_state == FALSE) {
    GstStateChangeReturn sret;

    sret = gst_element_set_state (priv->pipeline, GST_STATE_PLAYING);
    switch (sret) {
      case GST_STATE_CHANGE_FAILURE:
        /* ignore, we should get an error message posted on the bus */
        g_print ("Pipeline failed to go to PLAYING state\n");
        gst_element_set_state (priv->pipeline, GST_STATE_NULL);

        return FALSE;
      case GST_STATE_CHANGE_NO_PREROLL:
        g_print ("Pipeline is live.\n");
        priv->is_live = TRUE;
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

  return TRUE;
}

static void
_startup (GApplication * application)
{
  GstValidator *self = GST_VALIDATOR (application);

#ifdef G_OS_UNIX
  self->priv->signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, self);
#endif

  if (!_launch_pipeline (self))
    goto failure;

  G_APPLICATION_CLASS (gst_validator_parent_class)->startup (application);

  return;

failure:

  G_APPLICATION_CLASS (gst_validator_parent_class)->startup (application);

  self->priv->exit_code = 1;
}

static void
_shutdown (GApplication * application)
{
  GstValidator *self = GST_VALIDATOR (application);
  GstValidatorPrivate *priv = self->priv;

  if (self->priv->pipeline)
    gst_element_set_state (GST_ELEMENT (self->priv->pipeline), GST_STATE_NULL);

  if (priv->bus) {
    /* Clean the bus */
    gst_bus_set_flushing (priv->bus, TRUE);
    gst_bus_remove_signal_watch (priv->bus);
  }

  g_clear_object (&priv->pipeline);
  g_clear_object (&priv->runner);
  g_clear_object (&priv->monitor);
  g_clear_object (&priv->bus);


  if (self->priv->exit_code == 0)
    self->priv->exit_code = gst_validate_runner_printf (priv->runner);

#ifdef G_OS_UNIX
  g_source_remove (self->priv->signal_watch_id);
#endif

  G_APPLICATION_CLASS (gst_validator_parent_class)->shutdown (application);
}

static void
_finalize (GObject * object)
{
  gst_deinit ();
}

static void
gst_validator_class_init (GstValidatorClass * klass)
{
  GObjectClass *objclass = G_OBJECT_CLASS (klass);
  GApplicationClass *appclass = G_APPLICATION_CLASS (klass);

  objclass->finalize = _finalize;

  appclass->local_command_line = _local_command_line;
  appclass->startup = _startup;
  appclass->shutdown = _shutdown;

  _signals[CREATE_PIPELINE] =
      g_signal_new ("create-pipeline", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstValidatorClass, create_pipeline),
      NULL, NULL, g_cclosure_marshal_generic, GST_TYPE_ELEMENT, 3, G_TYPE_INT,
      G_TYPE_STRV, G_TYPE_POINTER);

  _signals[REGISTER_EXTRA_ACTION_TYPES] =
      g_signal_new ("register-action-types", G_TYPE_FROM_CLASS (klass),
      G_SIGNAL_RUN_LAST, G_STRUCT_OFFSET (GstValidatorClass,
          register_extra_action_types), NULL, NULL, g_cclosure_marshal_generic,
      G_TYPE_BOOLEAN, 1, GST_TYPE_ELEMENT);

  g_type_class_add_private (klass, sizeof (GstValidatorPrivate));
}

static void
gst_validator_init (GstValidator * self)
{
  self->priv = G_TYPE_INSTANCE_GET_PRIVATE (self,
      GST_TYPE_VALIDATOR, GstValidatorPrivate);
}

gint
gst_validator_get_exit_status (GstValidator * self)
{
  return self->priv->exit_code;
}

GstValidator *
gst_validator_new (const gchar * name)
{
  GstValidator *validator;
  gchar *id = g_strdup_printf ("org.gstreamer.validate.%s", name);

  validator =
      GST_VALIDATOR (g_object_new (gst_validator_get_type (), "application-id",
          id, "flags",
          G_APPLICATION_NON_UNIQUE | G_APPLICATION_HANDLES_COMMAND_LINE, NULL));

  g_free (id);


  return validator;
}
