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

#include "gst-validator.h"
#include <gst/validate/gst-validate-utils.h>
#include <gst/validate/gst-validate-scenario.h>

static gboolean
_is_playbin_pipeline (GstElement * pipeline)
{
  GstElementFactory *factory;

  if (!pipeline)
    return TRUE;

  factory = gst_element_get_factory (pipeline);

  if (g_strcmp0 (GST_OBJECT_NAME (factory), "playbin") == 0)
    return TRUE;

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

static GstPadProbeReturn
_check_pad_selection_done (GstPad * pad, GstPadProbeInfo * info,
    GstValidateAction * action)
{
  if (GST_BUFFER_FLAG_IS_SET (info->data, GST_BUFFER_FLAG_DISCONT)) {
    gst_validate_action_set_done (action);

    return GST_PAD_PROBE_REMOVE;
  }

  return GST_PAD_PROBE_OK;
}

static gboolean
_execute_switch_track (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  gint index, n;
  GstPad *srcpad;
  GstElement *combiner;
  GstPad *oldpad, *newpad;
  const gchar *type, *str_index;

  gint flags, current, tflag;
  gchar *tmp, *current_txt;

  gint res = GST_VALIDATE_EXECUTE_ACTION_OK;
  gboolean relative = FALSE, disabling = FALSE;

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
    GstState state, next;
    tmp = g_strdup_printf ("get-%s-pad", type);
    g_signal_emit_by_name (G_OBJECT (scenario->pipeline), tmp, current,
        &oldpad);
    g_signal_emit_by_name (G_OBJECT (scenario->pipeline), tmp, index, &newpad);

    gst_validate_printf (action, "Switching to track number: %i,"
        " (from %s:%s to %s:%s)\n", index, GST_DEBUG_PAD_NAME (oldpad),
        GST_DEBUG_PAD_NAME (newpad));
    flags |= tflag;
    g_free (tmp);

    if (gst_element_get_state (scenario->pipeline, &state, &next, 0) &&
        state == GST_STATE_PLAYING && next == GST_STATE_VOID_PENDING) {

      combiner = GST_ELEMENT (gst_object_get_parent (GST_OBJECT (newpad)));
      srcpad = gst_element_get_static_pad (combiner, "src");
      gst_object_unref (combiner);

      gst_pad_add_probe (srcpad,
          GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST,
          (GstPadProbeCallback) _check_pad_selection_done, action, NULL);
      gst_object_unref (srcpad);

      res = GST_VALIDATE_EXECUTE_ACTION_ASYNC;
    }

  } else {
    gst_validate_printf (action, "Disabling track type %s", type);
  }

  g_object_set (scenario->pipeline, "flags", flags, current_txt, index, NULL);
  g_free (current_txt);

  return res;
}

static gboolean
_register_playbin_actions (GstValidator * validator, GstElement * pipeline)
{

  if (!_is_playbin_pipeline (pipeline))
    return TRUE;

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

  return TRUE;
}

static GstElement *
_create_pipeline (GstValidator * validator, gint argc, gchar ** argv,
    GError ** error)
{
  GstElement *pipeline;

  gchar **argvn = g_new0 (char *, argc);
  memcpy (argvn, argv + 1, sizeof (char *) * (argc - 1));

  pipeline = (GstElement *) gst_parse_launchv ((const gchar **) argvn, error);

  g_free (argvn);

  GST_ERROR ("Returning Pipeline %p", pipeline);

  return pipeline;
}

int
main (int argc, gchar ** argv)
{
  gboolean ret;
  GstValidator *validator = gst_validator_new ("launcher");

  g_signal_connect (validator, "create-pipeline", G_CALLBACK (_create_pipeline),
      NULL);
  g_signal_connect (validator, "register-action-types",
      G_CALLBACK (_register_playbin_actions), NULL);

  ret = g_application_run (G_APPLICATION (validator), argc, argv);
  if (!ret)
    ret = gst_validator_get_exit_status (validator);

  g_print ("\n=======> Test %s (Return value: %i)\n\n",
      ret == 0 ? "PASSED" : "FAILED", ret);
  return ret;
}
