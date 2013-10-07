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

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/validate/validate.h>
#include <gst/pbutils/encoding-profile.h>

#ifdef G_OS_UNIX
#include <glib-unix.h>
#endif

#include <gst/validate/gst-validate-scenario.h>

static gint ret = 0;
static GMainLoop *mainloop;
static GstElement *pipeline;
static GstEncodingProfile *encoding_profile = NULL;
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
_execute_set_restriction (GstValidateScenario * scenario,
    GstValidateAction * action)
{
  GstCaps *caps;
  GType profile_type = G_TYPE_NONE;
  const gchar *restriction_caps, *profile_type_name, *profile_name;

  restriction_caps =
      gst_structure_get_string (action->structure, "restriction-caps");
  profile_type_name =
      gst_structure_get_string (action->structure, "profile-type");
  profile_name = gst_structure_get_string (action->structure, "profile-name");

  if (profile_type_name) {
    profile_type = g_type_from_name (profile_type_name);

    if (profile_type == G_TYPE_NONE) {
      g_error ("Profile name %s not known", profile_name);

      return FALSE;
    } else if (profile_type == GST_TYPE_ENCODING_CONTAINER_PROFILE) {
      g_error ("Can not set restrictions on container profiles");

      return FALSE;
    }
  } else if (profile_name == NULL) {
    if (g_strrstr (restriction_caps, "audio/x-raw") == restriction_caps)
      profile_type = GST_TYPE_ENCODING_AUDIO_PROFILE;
    else if (g_strrstr (restriction_caps, "video/x-raw") == restriction_caps)
      profile_type = GST_TYPE_ENCODING_VIDEO_PROFILE;
    else {
      g_error
          ("No information on what profiles to apply action, you should set either"
          "profile_name or profile_type_name and the caps %s give us no hint",
          restriction_caps);

      return FALSE;
    }
  }

  caps = gst_caps_from_string (restriction_caps);
  if (caps == NULL) {
    g_error ("Could not parse caps: %s", restriction_caps);

    return FALSE;
  }

  if (GST_IS_ENCODING_CONTAINER_PROFILE (encoding_profile)) {
    gboolean found = FALSE;
    const GList *tmp;

    for (tmp =
        gst_encoding_container_profile_get_profiles
        (GST_ENCODING_CONTAINER_PROFILE (encoding_profile)); tmp;
        tmp = tmp->next) {
      GstEncodingProfile *profile = tmp->data;

      if (profile_type != G_TYPE_NONE
          && G_OBJECT_TYPE (profile) == profile_type) {
        gst_encoding_profile_set_restriction (profile, gst_caps_copy (caps));
        found = TRUE;
      } else if (profile_name
          && g_strcmp0 (gst_encoding_profile_get_name (profile),
              profile_name) == 0) {
        gst_encoding_profile_set_restriction (profile, gst_caps_copy (caps));
        found = TRUE;
      }
    }

    if (!found) {
      g_error ("Could not find profile for %s%s",
          profile_type_name ? profile_type_name : "",
          profile_name ? profile_name : "");

      gst_caps_unref (caps);
      return FALSE;

    }
  }

  if (profile_type != G_TYPE_NONE) {
    g_print ("\n%s (num %u), setting caps to %s on profiles of type %s\n",
        action->name, action->action_number, restriction_caps,
        g_type_name (profile_type));
  } else {
    g_print ("\n%s (num %u), setting caps to %s on profile %s\n",
        action->name, action->action_number, restriction_caps, profile_name);

  }

  gst_caps_unref (caps);
  return TRUE;
}

static gboolean
print_position (void)
{
  GstQuery *query;
  gint64 position, duration;

  gdouble rate = 1.0;
  GstFormat format = GST_FORMAT_TIME;

  gst_element_query_position (pipeline, &format, &position);

  format = GST_FORMAT_TIME;
  gst_element_query_duration (pipeline, &format, &duration);

  query = gst_query_new_segment (GST_FORMAT_DEFAULT);
  if (gst_element_query (pipeline, query))
    gst_query_parse_segment (query, &rate, NULL, NULL, NULL);
  gst_query_unref (query);

  g_print ("<position: %" GST_TIME_FORMAT " duration: %" GST_TIME_FORMAT
      " speed: %f />\r", GST_TIME_ARGS (position), GST_TIME_ARGS (duration),
      rate);

  GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
      GST_DEBUG_GRAPH_SHOW_ALL, "position");

  return TRUE;
}

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
      GError *err;
      gchar *debug;
      ret = -1;
      gst_message_parse_error (message, &err, &debug);
      g_print ("Error: %s %s\n", GST_OBJECT_NAME (GST_MESSAGE_SRC (message)),
          err->message);
      g_error_free (err);
      g_free (debug);
      g_main_loop_quit (loop);
      break;
    }
    case GST_MESSAGE_EOS:
      g_main_loop_quit (loop);
      break;
    default:
      break;
  }

  return TRUE;
}

static void
pad_added_cb (GstElement * uridecodebin, GstPad * pad, GstElement * encodebin)
{
  GstCaps *caps;
  GstPad *sinkpad = NULL;

  GST_DEBUG_OBJECT (uridecodebin, "Pad added, caps: %" GST_PTR_FORMAT,
      gst_pad_get_caps (pad));


  /* Ask encodebin for a compatible pad */
  caps = gst_pad_get_caps_reffed (pad);
  g_signal_emit_by_name (encodebin, "request-pad", caps, &sinkpad);
  if (caps)
    gst_caps_unref (caps);

  if (sinkpad == NULL) {
    GST_WARNING ("Couldn't get an encoding pad for pad %s:%s\n",
        GST_DEBUG_PAD_NAME (pad));
    return;
  }

  if (G_UNLIKELY (gst_pad_link (pad, sinkpad) != GST_PAD_LINK_OK))
    GST_ERROR ("Couldn't link pads \n\n%" GST_PTR_FORMAT "\n\n  and \n\n %"
        GST_PTR_FORMAT "\n\n", gst_pad_get_caps (pad),
        gst_pad_get_caps (sinkpad));

  return;
}

static void
create_transcoding_pipeline (gchar * uri, gchar * outuri)
{
  GstElement *src, *ebin, *sink;

  mainloop = g_main_loop_new (NULL, FALSE);

  pipeline = gst_pipeline_new ("encoding-pipeline");
  src = gst_element_factory_make ("uridecodebin", NULL);

  ebin = gst_element_factory_make ("encodebin", NULL);
  sink = gst_element_make_from_uri (GST_URI_SINK, outuri, "sink");
  g_assert (sink);

  g_object_set (src, "uri", uri, NULL);
  g_object_set (ebin, "profile", encoding_profile, NULL);

  g_signal_connect (src, "pad-added", G_CALLBACK (pad_added_cb), ebin);

  gst_bin_add_many (GST_BIN (pipeline), src, ebin, sink, NULL);
  gst_element_link (ebin, sink);
}

static gboolean
_parse_encoding_profile (const gchar * option_name, const gchar * value,
    gpointer udata, GError ** error)
{
  GstCaps *caps;
  char *preset_name = NULL;
  gchar **restriction_format, **preset_v;

  guint i, presence = 0;
  GstCaps *restrictioncaps = NULL;
  gchar **strpresence_v, **strcaps_v = g_strsplit (value, ":", 0);

  if (strcaps_v[0] && *strcaps_v[0]) {
    caps = gst_caps_from_string (strcaps_v[0]);
    if (caps == NULL) {
      g_printerr ("Could not parse caps %s", strcaps_v[0]);
      return FALSE;
    }
    encoding_profile =
        GST_ENCODING_PROFILE (gst_encoding_container_profile_new
        ("User profile", "User profile", caps, NULL));
    gst_caps_unref (caps);
  } else {
    encoding_profile = NULL;
  }

  for (i = 1; strcaps_v[i]; i++) {
    GstEncodingProfile *profile = NULL;
    gchar *strcaps, *strpresence;

    restriction_format = g_strsplit (strcaps_v[i], "->", 0);
    if (restriction_format[1]) {
      restrictioncaps = gst_caps_from_string (restriction_format[0]);
      strcaps = g_strdup (restriction_format[1]);
    } else {
      restrictioncaps = NULL;
      strcaps = g_strdup (restriction_format[0]);
    }
    g_strfreev (restriction_format);

    preset_v = g_strsplit (strcaps, "+", 0);
    if (preset_v[1]) {
      strpresence = preset_v[1];
      g_free (strcaps);
      strcaps = g_strdup (preset_v[0]);
    } else {
      strpresence = preset_v[0];
    }

    strpresence_v = g_strsplit (strpresence, "|", 0);
    if (strpresence_v[1]) {     /* We have a presence */
      gchar *endptr;

      if (preset_v[1]) {        /* We have preset and presence */
        preset_name = g_strdup (strpresence_v[0]);
      } else {                  /* We have a presence but no preset */
        g_free (strcaps);
        strcaps = g_strdup (strpresence_v[0]);
      }

      presence = strtoll (strpresence_v[1], &endptr, 10);
      if (endptr == strpresence_v[1]) {
        g_printerr ("Wrong presence %s\n", strpresence_v[1]);

        return FALSE;
      }
    } else {                    /* We have no presence */
      if (preset_v[1]) {        /* Not presence but preset */
        preset_name = g_strdup (preset_v[1]);
        g_free (strcaps);
        strcaps = g_strdup (preset_v[0]);
      }                         /* Else we have no presence nor preset */
    }
    g_strfreev (strpresence_v);
    g_strfreev (preset_v);

    GST_DEBUG ("Creating preset with restrictions: %" GST_PTR_FORMAT
        ", caps: %s, preset %s, presence %d", restrictioncaps, strcaps,
        preset_name ? preset_name : "none", presence);

    caps = gst_caps_from_string (strcaps);
    g_free (strcaps);
    if (caps == NULL) {
      g_warning ("Could not create caps for %s", strcaps_v[i]);

      return FALSE;
    }

    if (g_str_has_prefix (strcaps_v[i], "audio/")) {
      profile = GST_ENCODING_PROFILE (gst_encoding_audio_profile_new (caps,
              preset_name, restrictioncaps, presence));
    } else if (g_str_has_prefix (strcaps_v[i], "video/") ||
        g_str_has_prefix (strcaps_v[i], "image/")) {
      profile = GST_ENCODING_PROFILE (gst_encoding_video_profile_new (caps,
              preset_name, restrictioncaps, presence));
    }

    g_free (preset_name);
    gst_caps_unref (caps);
    if (restrictioncaps)
      gst_caps_unref (restrictioncaps);

    if (profile == NULL) {
      g_warning ("No way to create a preset for caps: %s", strcaps_v[i]);

      return FALSE;
    }

    if (encoding_profile) {
      if (gst_encoding_container_profile_add_profile
          (GST_ENCODING_CONTAINER_PROFILE (encoding_profile),
              profile) == FALSE) {
        g_warning ("Can not create a preset for caps: %s", strcaps_v[i]);

        return FALSE;
      }
    } else {
      encoding_profile = profile;
    }
  }
  g_strfreev (strcaps_v);

  return TRUE;
}

int
main (int argc, gchar ** argv)
{
  guint i;
  GSList *tmp;
  GstBus *bus;
  GstValidateRunner *runner;
  GstValidateMonitor *monitor;
  GOptionContext *ctx;
#ifdef G_OS_UNIX
  guint signal_watch_id;
#endif

  GError *err = NULL;
  const gchar *scenario = NULL;
  guint count = 0;
  gboolean want_help = FALSE;
  gboolean list_scenarios = FALSE;

  const gchar *resize_video_mandatory_fields[] = { "restriction-caps", NULL };
  GOptionEntry options[] = {
    {"output-format", 'o', 0, G_OPTION_ARG_CALLBACK, &_parse_encoding_profile,
          "Set the properties to use for the encoding profile "
          "(in case of transcoding.) For example:\n"
          "video/mpegts:video/x-raw-yuv,width=1920,height=1080->video/x-h264:audio/x-ac3\n"
          "A preset name can be used by adding +presetname, eg:\n"
          "video/webm:video/x-vp8+mypreset:audio/x-vorbis\n"
          "The presence property of the profile can be specified with |<presence>, eg:\n"
          "video/webm:video/x-vp8|<presence>:audio/x-vorbis\n",
        "properties-values"},
    {"set-scenario", '\0', 0, G_OPTION_ARG_STRING, &scenario,
        "Let you set a scenario, it will override the GST_VALIDATE_SCENARIO "
          "environment variable", NULL},
    {"eos-on-shutdown", 'e', 0, G_OPTION_ARG_NONE, &eos_on_shutdown,
        "If an EOS event should be sent to the pipeline if an interrupt is "
          "received, instead of forcing the pipeline to stop. Sending an EOS "
          "will allow the transcoding to finish the files properly before "
          "exiting.", NULL},
    {"list-scenarios", 'l', 0, G_OPTION_ARG_NONE, &list_scenarios,
        "List the avalaible scenarios that can be run", NULL},
    {NULL}
  };

  /* There is a bug that make gst_init remove the help param when initializing,
   * it is FIXED in 1.0 */
  for (i = 1; i < argc; i++) {
    if (!g_strcmp0 (argv[i], "--help") || !g_strcmp0 (argv[i], "-h"))
      want_help = TRUE;
  }

  if (!want_help)
    gst_init (&argc, &argv);

  g_set_prgname ("gst-validate-transcoding-" GST_API_VERSION);
  ctx = g_option_context_new ("[input-uri] [output-uri]");
  g_option_context_set_summary (ctx, "Transcodes input-uri to output-uri, "
      "using the given encoding profile. The pipeline will be monitored for "
      "possible issues detection using the gst-validate lib."
      "\nCan also perform file conformance"
      "tests after transcoding to make sure the result is correct");
  g_option_context_add_main_entries (ctx, options, NULL);
  if (want_help) {
    g_option_context_add_group (ctx, gst_init_get_option_group ());
  }

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    exit (1);
  }

  g_option_context_free (ctx);

  if (scenario)
    g_setenv ("GST_VALIDATE_SCENARIO", scenario, TRUE);

  if (list_scenarios)
    gst_validate_list_scenarios ();

  gst_validate_init ();

  gst_validate_add_action_type ("set-restriction", _execute_set_restriction,
      resize_video_mandatory_fields, "Change the restriction caps on the fly");

  if (argc != 3) {
    g_printerr ("%i arguments recived, 2 expected.\n"
        "You should run the test using:\n"
        "    ./gst-validate-transcoding-0.10 <input-uri> <output-uri> [options]\n",
        argc - 1);
    return 1;
  }

  if (encoding_profile == NULL) {
    GST_INFO ("Creating default encoding profile");

    _parse_encoding_profile ("encoding-profile",
        "application/ogg:video/x-theora:audio/x-vorbis", NULL, NULL);
  }

  /* Create the pipeline */
  create_transcoding_pipeline (argv[1], argv[2]);

#ifdef G_OS_UNIX
  signal_watch_id =
      g_unix_signal_add (SIGINT, (GSourceFunc) intr_handler, pipeline);
#endif

  runner = gst_validate_runner_new ();
  monitor =
      gst_validate_monitor_factory_create (GST_OBJECT_CAST (pipeline), runner,
      NULL);
  mainloop = g_main_loop_new (NULL, FALSE);

  if (!runner) {
    g_printerr ("Failed to setup Validate Runner\n");
    exit (1);
  }

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message", (GCallback) bus_callback, mainloop);
  gst_object_unref (bus);

  g_print ("Starting pipeline\n");
  if (gst_element_set_state (pipeline,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE)
    goto exit;
  g_timeout_add (50, (GSourceFunc) print_position, NULL);
  g_main_loop_run (mainloop);

  for (tmp = gst_validate_runner_get_reports (runner); tmp; tmp = tmp->next) {
    if (ret == 0 && ((GstValidateReport *) (tmp->data))->level ==
        GST_VALIDATE_REPORT_LEVEL_CRITICAL) {
      g_printerr ("Got critical error %s, setting return value to -1\n",
          ((GstValidateReport *) (tmp->data))->message);
      ret = -1;
    }
    count++;
  }
  g_print ("Pipeline finished, total issues found: %u\n", count);
  if (count) {
    GSList *iter;
    GSList *issues = gst_validate_runner_get_reports (runner);

    for (iter = issues; iter; iter = g_slist_next (iter)) {
      GstValidateReport *report = iter->data;
      gst_validate_report_printf (report);
    }
  }

exit:
  gst_element_set_state (pipeline, GST_STATE_NULL);
  g_main_loop_unref (mainloop);
  g_object_unref (monitor);
  g_object_unref (runner);
  g_object_unref (pipeline);

#ifdef G_OS_UNIX
  g_source_remove (signal_watch_id);
#endif

  return ret;
}
