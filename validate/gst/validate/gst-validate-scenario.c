/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-validate-scenario.c - Validate Scenario class
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
#include "config.h"
#endif

#include <gst/gst.h>
#include <gio/gio.h>
#include <string.h>

#include "gst-validate-internal.h"
#include "gst-validate-scenario.h"
#include "gst-validate-reporter.h"
#include "gst-validate-report.h"

#define GST_VALIDATE_SCENARIO_GET_PRIVATE(o) \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), GST_TYPE_VALIDATE_SCENARIO, GstValidateScenarioPrivate))

#define GST_VALIDATE_SCENARIO_SUFFIX ".scenario"
#define GST_VALIDATE_SCENARIO_DIRECTORY "validate-scenario"

#define DEFAULT_SEEK_TOLERANCE (0.1 * GST_SECOND)       /* tolerance seek interval
                                                           TODO make it overridable  */
enum
{
  PROP_0,
  PROP_RUNNER,
  PROP_LAST
};

static GHashTable *action_types_table;
static void gst_validate_scenario_dispose (GObject * object);
static void gst_validate_scenario_finalize (GObject * object);

G_DEFINE_TYPE_WITH_CODE (GstValidateScenario, gst_validate_scenario,
    G_TYPE_OBJECT, G_IMPLEMENT_INTERFACE (GST_TYPE_VALIDATE_REPORTER, NULL));

struct _GstValidateScenarioPrivate
{
  GstElement *pipeline;
  GstValidateRunner *runner;

  GList *actions;
  gint64 seeked_position;       /* last seeked position */
  GstClockTime seek_pos_tol;

  guint num_actions;

  guint get_pos_id;
};


/* Some helper method that are missing iin Json itscenario */
static guint
get_flags_from_string (GType type, const gchar * str_flags)
{
  guint i;
  gint flags = 0;
  GFlagsClass *class = g_type_class_ref (type);

  for (i = 0; i < class->n_values; i++) {
    if (g_strrstr (str_flags, class->values[i].value_nick)) {
      flags |= class->values[i].value;
    }
  }
  g_type_class_unref (class);

  return flags;
}

static void
get_enum_from_string (GType type, const gchar * str_enum, guint * enum_value)
{
  guint i;
  GEnumClass *class = g_type_class_ref (type);

  for (i = 0; i < class->n_values; i++) {
    if (g_strrstr (str_enum, class->values[i].value_nick)) {
      *enum_value = class->values[i].value;
      break;
    }
  }

  g_type_class_unref (class);
}

static void
_free_scenario_action (GstValidateAction * act)
{
  if (act->structure)
    gst_structure_free (act->structure);

  g_slice_free (GstValidateAction, act);
}

static gboolean
_execute_seek (GstValidateScenario * scenario, GstValidateAction * action)
{
  GstValidateScenarioPrivate *priv = scenario->priv;
  const char *str_format, *str_flags, *str_start_type, *str_stop_type;

  gdouble rate = 1.0, dstart, dstop;
  GstFormat format = GST_FORMAT_TIME;
  GstSeekFlags flags = GST_SEEK_FLAG_ACCURATE | GST_SEEK_FLAG_FLUSH;
  GstSeekType start_type = GST_SEEK_TYPE_SET;
  GstClockTime start;
  GstSeekType stop_type = GST_SEEK_TYPE_SET;
  GstClockTime stop = GST_CLOCK_TIME_NONE;

  if (!gst_structure_get_double (action->structure, "start", &dstart)) {
    GST_WARNING_OBJECT (scenario, "Could not find start for a seek, FAILED");
    return FALSE;
  }
  start = dstart * GST_SECOND;

  gst_structure_get_double (action->structure, "rate", &rate);
  if ((str_format = gst_structure_get_string (action->structure, "format")))
    get_enum_from_string (GST_TYPE_FORMAT, str_format, &format);

  if ((str_start_type =
          gst_structure_get_string (action->structure, "start_type")))
    get_enum_from_string (GST_TYPE_SEEK_TYPE, str_start_type, &start_type);

  if ((str_stop_type =
          gst_structure_get_string (action->structure, "stop_type")))
    get_enum_from_string (GST_TYPE_SEEK_TYPE, str_stop_type, &stop_type);

  if ((str_flags = gst_structure_get_string (action->structure, "flags")))
    flags = get_flags_from_string (GST_TYPE_SEEK_FLAGS, str_flags);

  if (gst_structure_get_double (action->structure, "stop", &dstop))
    stop = dstop * GST_SECOND;

  g_print ("%s (num %u), seeking to: %" GST_TIME_FORMAT " stop: %"
      GST_TIME_FORMAT " Rate %lf\n", action->name,
      action->action_number, GST_TIME_ARGS (start), GST_TIME_ARGS (stop), rate);

  priv->seeked_position = (rate > 0) ? start : stop;
  if (gst_element_seek (priv->pipeline, rate, format, flags, start_type, start,
          stop_type, stop) == FALSE) {
    GST_VALIDATE_REPORT (scenario, EVENT_SEEK_NOT_HANDLED,
        "Could not seek to position %" GST_TIME_FORMAT,
        GST_TIME_ARGS (priv->seeked_position));
    return FALSE;
  }

  return TRUE;
}

static gboolean
_pause_action_restore_playing (GstValidateScenario * scenario)
{
  GstElement *pipeline = scenario->priv->pipeline;

  if (gst_element_set_state (pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_VALIDATE_REPORT (scenario, STATE_CHANGE_FAILURE,
        "Failed to set state to playing");
  }

  return FALSE;
}


static gboolean
_execute_pause (GstValidateScenario * scenario, GstValidateAction * action)
{
  gdouble duration = 0;

  GstValidateScenarioPrivate *priv = scenario->priv;

  gst_structure_get_double (action->structure, "duration", &duration);
  g_print ("\n%s (num %u), pausing for %" GST_TIME_FORMAT "\n",
      action->name, action->action_number,
      GST_TIME_ARGS (duration * GST_SECOND));

  GST_DEBUG ("Pausing for %" GST_TIME_FORMAT,
      GST_TIME_ARGS (duration * GST_SECOND));

  if (gst_element_set_state (priv->pipeline, GST_STATE_PAUSED) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_VALIDATE_REPORT (scenario, STATE_CHANGE_FAILURE,
        "Failed to set state to paused");

    return FALSE;
  }
  gst_element_get_state (priv->pipeline, NULL, NULL, -1);
  if (duration)
    g_timeout_add (duration * 1000,
        (GSourceFunc) _pause_action_restore_playing, scenario);

  return TRUE;
}

static gboolean
_execute_play (GstValidateScenario * scenario, GstValidateAction * action)
{
  g_print ("\n%s (num %u), Playing back", action->name, action->action_number);

  GST_DEBUG ("Playing back");

  if (gst_element_set_state (scenario->priv->pipeline, GST_STATE_PLAYING) ==
      GST_STATE_CHANGE_FAILURE) {
    GST_VALIDATE_REPORT (scenario, STATE_CHANGE_FAILURE,
        "Failed to set state to playing");

    return FALSE;
  }
  gst_element_get_state (scenario->priv->pipeline, NULL, NULL, -1);
  return TRUE;
}

static gboolean
_execute_eos (GstValidateScenario * scenario, GstValidateAction * action)
{
  g_print ("\n%s (num %u), sending EOS at %" GST_TIME_FORMAT "\n",
      action->name, action->action_number,
      GST_TIME_ARGS (action->playback_time));

  GST_DEBUG ("Sending eos to pipeline at %" GST_TIME_FORMAT,
      GST_TIME_ARGS (action->playback_time));

  return gst_element_send_event (scenario->priv->pipeline,
      gst_event_new_eos ());
}


static gboolean
get_position (GstValidateScenario * scenario)
{
  GList *tmp;
  GstQuery *query;
  gdouble rate = 1.0;
  GstValidateAction *act;
  gint64 position, duration;
  GstFormat format = GST_FORMAT_TIME;
  GstValidateScenarioPrivate *priv = scenario->priv;
  GstElement *pipeline = scenario->priv->pipeline;

  if (scenario->priv->actions == NULL) {
    GST_DEBUG_OBJECT (scenario,
        "No more actions to execute, stop calling  get_position");
    return FALSE;
  }

  query = gst_query_new_segment (GST_FORMAT_DEFAULT);
  if (gst_element_query (GST_ELEMENT (priv->pipeline), query))
    gst_query_parse_segment (query, &rate, NULL, NULL, NULL);

  gst_query_unref (query);
  act = scenario->priv->actions->data;
  gst_element_query_position (pipeline, &format, &position);

  format = GST_FORMAT_TIME;
  gst_element_query_duration (pipeline, &format, &duration);

  if (position > duration) {
    GST_VALIDATE_REPORT (scenario,
        QUERY_POSITION_SUPERIOR_DURATION,
        "Reported position %" GST_TIME_FORMAT " > reported duration %"
        GST_TIME_FORMAT, GST_TIME_ARGS (position), GST_TIME_ARGS (duration));

    return TRUE;
  }


  GST_LOG ("Current position: %" GST_TIME_FORMAT, GST_TIME_ARGS (position));
  if ((rate > 0 && (GstClockTime) position >= act->playback_time) ||
      (rate < 0 && (GstClockTime) position <= act->playback_time)) {
    GstValidateExecuteAction func;

    /* TODO what about non flushing seeks? */
    /* TODO why is this inside the action time if ? */
    if (GST_CLOCK_TIME_IS_VALID (priv->seeked_position))
      return TRUE;

    func = g_hash_table_lookup (action_types_table, act->type);
    func (scenario, act);

    tmp = priv->actions;
    priv->actions = g_list_remove_link (priv->actions, tmp);
    _free_scenario_action (act);
    g_list_free (tmp);
  }

  return TRUE;
}

static gboolean
async_done_cb (GstBus * bus, GstMessage * message,
    GstValidateScenario * scenario)
{
  GstValidateScenarioPrivate *priv = scenario->priv;

  if (GST_CLOCK_TIME_IS_VALID (priv->seeked_position)) {
    gint64 position;
    GstFormat format = GST_FORMAT_TIME;

    gst_element_query_position (priv->pipeline, &format, &position);
    if (position > (priv->seeked_position + priv->seek_pos_tol) ||
        position < (MAX (0,
                ((gint64) (priv->seeked_position - priv->seek_pos_tol))))) {

      GST_VALIDATE_REPORT (scenario, EVENT_SEEK_RESULT_POSITION_WRONG,
          "Seeked position %" GST_TIME_FORMAT " not in the expected range [%"
          GST_TIME_FORMAT " -- %" GST_TIME_FORMAT, GST_TIME_ARGS (position),
          GST_TIME_ARGS (((MAX (0,
                          ((gint64) (priv->seeked_position -
                                  priv->seek_pos_tol)))))),
          GST_TIME_ARGS ((priv->seeked_position + priv->seek_pos_tol)));
    }
    priv->seeked_position = GST_CLOCK_TIME_NONE;
  }

  if (priv->get_pos_id == 0) {
    get_position (scenario);
    priv->get_pos_id = g_timeout_add (50, (GSourceFunc) get_position, scenario);
  }


  return TRUE;
}

static gboolean
_load_scenario_file (GstValidateScenario * scenario,
    const gchar * scenario_file)
{
  guint i;
  gsize xmlsize;
  GFile *file = NULL;
  GError *err = NULL;
  gboolean ret = TRUE;
  gchar *content = NULL, **lines = NULL;
  GstValidateScenarioPrivate *priv = scenario->priv;
  gchar *uri = gst_filename_to_uri (scenario_file, &err);

  if (uri == NULL)
    goto failed;

  GST_DEBUG ("Trying to load %s", scenario_file);
  if ((file = g_file_new_for_path (scenario_file)) == NULL)
    goto wrong_uri;

  /* TODO Handle GCancellable */
  if (!g_file_load_contents (file, NULL, &content, &xmlsize, NULL, &err))
    goto failed;

  if (g_strcmp0 (content, "") == 0)
    goto failed;

  lines = g_strsplit (content, "\n", 0);
  for (i = 0; lines[i]; i++) {
    const gchar *type;
    gdouble playback_time;
    GstValidateAction *action;
    GstStructure *structure;

    if (g_strcmp0 (lines[i], "") == 0)
      continue;

    structure = gst_structure_from_string (lines[i], NULL);
    if (structure == NULL) {
      GST_WARNING_OBJECT (scenario, "Could not parse action %s", lines[i]);
      continue;
    }

    type = gst_structure_get_name (structure);
    if (!g_hash_table_lookup (action_types_table, type)) {
      GST_WARNING_OBJECT (scenario, "We do not handle action types %s", type);
    }

    action = g_slice_new0 (GstValidateAction);
    action->type = type;
    if (gst_structure_get_double (structure, "playback_time", &playback_time))
      action->playback_time = playback_time * GST_SECOND;
    else
      GST_WARNING_OBJECT (scenario, "No playback time for action %s", lines[i]);

    if (!(action->name = gst_structure_get_string (structure, "name")))
      action->name = "(no name)";

    action->action_number = priv->num_actions++;
    action->structure = structure;
    priv->actions = g_list_append (priv->actions, action);
  }

done:
  if (content)
    g_free (content);

  if (file)
    gst_object_unref (file);

  return ret;

wrong_uri:
  GST_WARNING ("%s wrong uri", scenario_file);

  ret = FALSE;
  goto done;

failed:
  ret = FALSE;
  if (err) {
    GST_WARNING ("Failed to load contents: %d %s", err->code, err->message);
    g_error_free (err);
  }
  goto done;
}

static gboolean
gst_validate_scenario_load (GstValidateScenario * scenario,
    const gchar * scenario_name)
{
  gboolean ret = TRUE;
  gchar *lfilename = NULL, *tldir = NULL;

  if (!scenario_name)
    goto invalid_name;

  lfilename =
      g_strdup_printf ("%s" GST_VALIDATE_SCENARIO_SUFFIX, scenario_name);

  /* Try from local profiles */
  tldir =
      g_build_filename (g_get_user_data_dir (), "gstreamer-" GST_API_VERSION,
      GST_VALIDATE_SCENARIO_DIRECTORY, lfilename, NULL);


  if (!(ret = _load_scenario_file (scenario, tldir))) {
    g_free (tldir);
    /* Try from system-wide profiles */
    tldir = g_build_filename (GST_DATADIR, "gstreamer-" GST_API_VERSION,
        GST_VALIDATE_SCENARIO_DIRECTORY, lfilename, NULL);
    ret = _load_scenario_file (scenario, tldir);
  }

  /* Hack to make it work uninstalled */
  if (ret == FALSE) {
    g_free (tldir);

    tldir = g_build_filename ("data/", lfilename, NULL);
    ret = _load_scenario_file (scenario, tldir);
  }

done:
  if (tldir)
    g_free (tldir);
  if (lfilename)
    g_free (lfilename);

  return ret;

invalid_name:
  {
    GST_ERROR ("Invalid name for scenario '%s'", scenario_name);
    ret = FALSE;
    goto done;
  }
}


static void
gst_validate_scenario_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      gst_validate_reporter_set_runner (GST_VALIDATE_REPORTER (object),
          g_value_get_object (value));
      break;
    default:
      break;
  }
}

static void
gst_validate_scenario_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  switch (prop_id) {
    case PROP_RUNNER:
      /* we assume the runner is valid as long as this scenario is,
       * no ref taken */
      g_value_set_object (value,
          gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER (object)));
      break;
    default:
      break;
  }
}

static void
gst_validate_scenario_class_init (GstValidateScenarioClass * klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  g_type_class_add_private (klass, sizeof (GstValidateScenarioPrivate));

  object_class->dispose = gst_validate_scenario_dispose;
  object_class->finalize = gst_validate_scenario_finalize;

  object_class->get_property = gst_validate_scenario_get_property;
  object_class->set_property = gst_validate_scenario_set_property;

  g_object_class_install_property (object_class, PROP_RUNNER,
      g_param_spec_object ("validate-runner", "VALIDATE Runner",
          "The Validate runner to " "report errors to",
          GST_TYPE_VALIDATE_RUNNER,
          G_PARAM_CONSTRUCT_ONLY | G_PARAM_READWRITE));
}

static void
gst_validate_scenario_init (GstValidateScenario * scenario)
{
  GstValidateScenarioPrivate *priv = scenario->priv =
      GST_VALIDATE_SCENARIO_GET_PRIVATE (scenario);


  priv->seeked_position = GST_CLOCK_TIME_NONE;
  priv->seek_pos_tol = DEFAULT_SEEK_TOLERANCE;
}

static void
gst_validate_scenario_dispose (GObject * object)
{
  GstValidateScenarioPrivate *priv = GST_VALIDATE_SCENARIO (object)->priv;

  if (priv->pipeline)
    gst_object_unref (priv->pipeline);
  g_list_free_full (priv->actions, (GDestroyNotify) _free_scenario_action);

  G_OBJECT_CLASS (gst_validate_scenario_parent_class)->dispose (object);
}

static void
gst_validate_scenario_finalize (GObject * object)
{
  G_OBJECT_CLASS (gst_validate_scenario_parent_class)->finalize (object);
}

GstValidateScenario *
gst_validate_scenario_factory_create (GstValidateRunner * runner,
    GstElement * pipeline, const gchar * scenario_name)
{
  GstBus *bus;
  GstValidateScenario *scenario =
      g_object_new (GST_TYPE_VALIDATE_SCENARIO, "validate-runner",
      runner, NULL);

  GST_LOG ("Creating scenario %s", scenario_name);
  if (!gst_validate_scenario_load (scenario, scenario_name)) {
    g_object_unref (scenario);

    return NULL;
  }

  scenario->priv->pipeline = gst_object_ref (pipeline);
  gst_validate_reporter_set_name (GST_VALIDATE_REPORTER (scenario),
      g_strdup (scenario_name));

  bus = gst_element_get_bus (pipeline);
  gst_bus_add_signal_watch (bus);
  g_signal_connect (bus, "message::async-done", (GCallback) async_done_cb,
      scenario);
  gst_object_unref (bus);

  g_print ("\n=========================================\n"
      "Running scenario %s on pipeline %s"
      "\n=========================================\n", scenario_name,
      GST_OBJECT_NAME (pipeline));

  return scenario;
}

static void
_list_scenarios_in_dir (GFile * dir)
{
  GFileEnumerator *fenum;
  GFileInfo *info;

  fenum = g_file_enumerate_children (dir, G_FILE_ATTRIBUTE_STANDARD_NAME,
      G_FILE_QUERY_INFO_NONE, NULL, NULL);

  if (fenum == NULL)
    return;

  for (info = g_file_enumerator_next_file (fenum, NULL, NULL);
      info; info = g_file_enumerator_next_file (fenum, NULL, NULL)) {
    if (g_str_has_suffix (g_file_info_get_name (info),
            GST_VALIDATE_SCENARIO_SUFFIX)) {
      gchar **name = g_strsplit (g_file_info_get_name (info),
          GST_VALIDATE_SCENARIO_SUFFIX, 0);

      g_print ("Scenario %s \n", name[0]);

      g_strfreev (name);
    }
  }
}

void
gst_validate_list_scenarios (void)
{
  gchar *tldir = g_build_filename (g_get_user_data_dir (),
      "gstreamer-" GST_API_VERSION, GST_VALIDATE_SCENARIO_DIRECTORY,
      NULL);
  GFile *dir = g_file_new_for_path (tldir);

  g_print ("====================\n"
      "Avalaible scenarios:\n" "====================\n");
  _list_scenarios_in_dir (dir);
  g_object_unref (dir);
  g_free (tldir);

  tldir = g_build_filename (GST_DATADIR, "gstreamer-" GST_API_VERSION,
      GST_VALIDATE_SCENARIO_DIRECTORY, NULL);
  dir = g_file_new_for_path (tldir);
  _list_scenarios_in_dir (dir);
  g_object_unref (dir);
  g_free (tldir);

  /* Hack to make it work uninstalled */
  dir = g_file_new_for_path ("data/");
  _list_scenarios_in_dir (dir);
  g_object_unref (dir);

}

void
gst_validate_add_action_type (const gchar *type_name, GstValidateExecuteAction function)
{
  g_hash_table_insert (action_types_table, g_strdup (type_name), function);
}

void
init_scenarios (void)
{
  action_types_table = g_hash_table_new_full (g_str_hash, g_str_equal,
      (GDestroyNotify) g_free, NULL);

  gst_validate_add_action_type ("seek", _execute_seek);
  gst_validate_add_action_type ("pause",_execute_pause);
  gst_validate_add_action_type ("play",_execute_play);
  gst_validate_add_action_type ("eos",_execute_eos);
}
