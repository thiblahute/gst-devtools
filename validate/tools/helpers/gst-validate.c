/* GStreamer
 *
 * Copyright (C) Thibault Saunier <thibault.saunier@collabora.com>
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

#include "gst-validate.h"

static gboolean
_is_playbin_pipeline (int argc, gchar ** argv)
{
  gint i;

  for (i = 0; i < argc; i++) {
    if (g_strcmp0 (argv[i], "playbin") == 0) {
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
  g_object_set (scenario->pipeline, "suburi", uri, NULL);
  g_free (uri);

  return TRUE;
}

static gboolean
_execute_switch_track (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  gint index, n;
  GstPad *oldpad, *newpad;
  gboolean relative = FALSE, disabling = FALSE;
  const gchar *type, *str_index;

  gint flags, current, tflag;
  gchar *tmp, *current_txt;

  if (!(type = gst_structure_get_string (action->structure, "type")))
    type = "audio";

  tflag =
      gst_validate_utils_flags_from_str (g_type_from_name ("GstPlayFlags"),
      type);
  current_txt = g_strdup_printf ("current-%s", type);

  tmp = g_strdup_printf ("n-%s", type);
  g_object_get (scenario->pipeline, "flags", &flags, tmp, &n,
      current_txt, &current, NULL);

  g_free (tmp);

  if (gst_structure_has_field (action->structure, "disable")) {
    disabling = TRUE;
    flags &= ~tflag;
    index = -1;
  } else if (!(str_index =
          gst_structure_get_string (action->structure, "index"))) {
    if (!gst_structure_get_int (action->structure, "index", &index)) {
      GST_WARNING ("No index given, defaulting to +1");
      index = 1;
      relative = TRUE;
    }
  } else {
    relative = strchr ("+-", str_index[0]) != NULL;
    index = g_ascii_strtoll (str_index, NULL, 10);
  }

  if (relative) {               /* We are changing track relatively to current track */
    index = current + index;
    if (current >= n)
      index = -2;
  }

  if (!disabling) {
    tmp = g_strdup_printf ("get-%s-pad", type);
    g_signal_emit_by_name (G_OBJECT (scenario->pipeline), tmp, current,
        &oldpad);
    g_signal_emit_by_name (G_OBJECT (scenario->pipeline), tmp, index, &newpad);

    gst_validate_printf (action, "Switching to track number: %i,"
        " (from %s:%s to %s:%s)\n", index, GST_DEBUG_PAD_NAME (oldpad),
        GST_DEBUG_PAD_NAME (newpad));
    flags |= tflag;
    g_free (tmp);
  } else {
    gst_validate_printf (action, "Disabling track type %s", type);
  }

  g_object_set (scenario->pipeline, "flags", flags, current_txt, index, NULL);
  g_free (current_txt);

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
      "The subtitles file that will be used should will be specified\n"
      "relatively to the playbin URI in use thanks to the subtitle-file\n"
      "action property. You can also specify a folder with subtitle-dir\n"
      "For example if playbin.uri='file://some/uri.mov\n"
      "and action looks like 'set-subtitle, subtitle-file=en.srt'\n"
      "the subtitle URI will be set to 'file:///some/uri.mov.en.srt'\n",
      FALSE);

  /* Overriding default implementation */
  gst_validate_register_action_type ("switch-track", "validate-launcher", _execute_switch_track,
      (GstValidateActionParameter []) {
        {
          .name = "type",
          .description = "Selects which track type to change (can be 'audio', 'video',"
                          " or 'text').",
          .mandatory = FALSE,
          .types = "string",
          .possible_variables = NULL,
          .def = "audio",
        },
        {
          .name = "index",
          .description = "Selects which track of this type to use: it can be either a number,\n"
                         "which will be the Nth track of the given type, or a number with a '+' or\n"
                         "'-' prefix, which means a relative change (eg, '+1' means 'next track',\n"
                         "'-1' means 'previous track')",
          .mandatory = FALSE,
          .types = "string: to switch track relatively\n"
                   "int: To use the actual index to use",
          .possible_variables = NULL,
          .def = "+1",
        },
        {NULL}
      },
      "The 'switch-track' command can be used to switch tracks.\n"
      "The 'type' argument selects which track type to change (can be 'audio', 'video',"
      " or 'text').\nThe 'index' argument selects which track of this type\n"
      "to use: it can be either a number, which will be the Nth track of\n"
      "the given type, or a number with a '+' or '-' prefix, which means\n"
      "a relative change (eg, '+1' means 'next track', '-1' means 'previous\n"
      "track'), note that you need to state that it is a string in the scenario file\n"
      "prefixing it with (string).", FALSE);
/* *INDENT-ON* */
}

GstElement *
gst_validate_build_pipeline (int argc, gchar ** argv, gchar ** error,
    GstValidateRunner ** runner, GstValidateMonitor ** monitor,
    GOptionEntry * extra_options)
{
  GError *err = NULL;
  gchar *output_file = NULL;
  GstElement *pipeline = NULL;
  const gchar *scenario = NULL, *configs = NULL, *media_info = NULL;
  gboolean list_scenarios = FALSE, inspect_action_type = FALSE;

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
    {"set-media-info", '\0', 0, G_OPTION_ARG_STRING, &media_info,
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
  GOptionContext *ctx;
  gchar **argvn;

  *error = NULL;
  *runner = NULL;
  *monitor = NULL;

#ifdef HAVE_CONFIG_H
  g_set_prgname ("gst-validate-" GST_API_VERSION);
#endif

  ctx = g_option_context_new ("PIPELINE-DESCRIPTION");
  g_option_context_add_main_entries (ctx, options, NULL);
  g_option_context_set_summary (ctx, "Runs a gst launch pipeline, adding "
      "monitors to it to identify issues in the used elements. At the end"
      " a report will be printed. To view issues as they are created, set"
      " the env var GST_DEBUG=validate:2 and it will be printed "
      "as gstreamer debugging");

  if (extra_options)
    g_option_context_add_main_entries (ctx, extra_options, NULL);

  if (argc == 1) {
    g_print ("%s", g_option_context_get_help (ctx, FALSE, NULL));

    return NULL;
  }

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    *error = g_strdup_printf ("Error initializing: %s", err->message);

    g_option_context_free (ctx);
    g_clear_error (&err);
    return NULL;
  }

  if (scenario || configs) {
    gchar *scenarios;

    if (scenario)
      scenarios = g_strjoin (":", scenario, configs, NULL);
    else
      scenarios = g_strdup (configs);

    g_setenv ("GST_VALIDATE_SCENARIO", scenarios, TRUE);
    g_free (scenarios);
  }

  gst_init (&argc, &argv);
  gst_validate_init ();

  if (list_scenarios || output_file) {
    if (gst_validate_list_scenarios (argv + 1, argc - 1, output_file))
      *error = g_strdup ("Could not list scenarios");

    return NULL;
  }

  if (inspect_action_type) {
    _register_playbin_actions ();

    if (!gst_validate_print_action_types ((const gchar **) argv + 1, argc - 1))
      *error = g_strdup ("Could not print all wanted types");

    return NULL;
  }

  if (argc == 1) {
    g_print ("%s", g_option_context_get_help (ctx, FALSE, NULL));
    g_option_context_free (ctx);

    *error = g_strdup ("Wrong parametters");

    return NULL;
  }

  g_option_context_free (ctx);

  /* Create the pipeline */
  argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));
  pipeline = (GstElement *) gst_parse_launchv ((const gchar **) argvn, &err);
  g_free (argvn);
  if (!pipeline) {
    *error = g_strdup_printf ("Unable to build pipeline: %s", err->message);
    g_clear_error (&err);

    return NULL;
  }

  if (!GST_IS_PIPELINE (pipeline)) {
    GstElement *new_pipeline = gst_pipeline_new ("");

    gst_bin_add (GST_BIN (new_pipeline), pipeline);
    pipeline = new_pipeline;
  }

  gst_pipeline_set_auto_flush_bus (GST_PIPELINE (pipeline), FALSE);

  if (_is_playbin_pipeline (argc, argv + 1)) {
    _register_playbin_actions ();
  }

  *runner = gst_validate_runner_new ();
  if (!*runner) {
    *error = g_strdup ("Failed to setup Validate Runner");
  }

  *monitor = gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline),
      *runner, NULL);
  gst_validate_reporter_set_handle_g_logs (GST_VALIDATE_REPORTER (*monitor));

  if (media_info) {
    GError *err = NULL;
    GstMediaDescriptorParser *parser = gst_media_descriptor_parser_new (*runner,
        media_info, &err);

    if (parser == NULL) {
      *error =
          g_strdup_printf ("Could not use %s as a media-info file (error: %s)",
          media_info, err ? err->message : "Unknown error");
      gst_object_unref (pipeline);
      gst_object_unref (*monitor);
      gst_object_unref (*runner);
      return NULL;
    }

    gst_validate_monitor_set_media_descriptor (*monitor,
        GST_MEDIA_DESCRIPTOR (parser));
    gst_object_unref (parser);
  }


  return pipeline;
}
