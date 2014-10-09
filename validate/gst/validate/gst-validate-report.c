/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-monitor-report.c - Validate report/issues functions
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

#include <stdio.h>              /* fprintf */
#include <glib/gstdio.h>
#include <errno.h>

#include <string.h>
#include "gst-validate-i18n-lib.h"
#include "gst-validate-internal.h"

#include "gst-validate-report.h"
#include "gst-validate-reporter.h"
#include "gst-validate-monitor.h"
#include "gst-validate-scenario.h"

static GstClockTime _gst_validate_report_start_time = 0;
static GstValidateDebugFlags _gst_validate_flags = 0;
static GHashTable *_gst_validate_issues = NULL;
static GList *print_funcs = NULL;

typedef struct _PipelineLog
{
  GstElement *pipeline;
  GstElement *src;

  gboolean is_std;
  FILE *file;
} PipelineLog;

static GArray *log_pipelines = NULL;


GRegex *newline_regex = NULL;

GST_DEBUG_CATEGORY_STATIC (gst_validate_report_debug);
#undef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gst_validate_report_debug

#define GST_VALIDATE_REPORT_SHADOW_REPORTS_LOCK(r)			\
  G_STMT_START {					\
  (g_mutex_lock (&((GstValidateReport *) r)->shadow_reports_lock));		\
  } G_STMT_END

#define GST_VALIDATE_REPORT_SHADOW_REPORTS_UNLOCK(r)			\
  G_STMT_START {					\
  (g_mutex_unlock (&((GstValidateReport *) r)->shadow_reports_lock));		\
  } G_STMT_END

G_DEFINE_BOXED_TYPE (GstValidateReport, gst_validate_report,
    (GBoxedCopyFunc) gst_validate_report_ref,
    (GBoxedFreeFunc) gst_validate_report_unref);

GstValidateIssueId
gst_validate_issue_get_id (GstValidateIssue * issue)
{
  return issue->issue_id;
}

GstValidateIssue *
gst_validate_issue_new (GstValidateIssueId issue_id, const gchar * summary,
    const gchar * description, GstValidateReportLevel default_level)
{
  GstValidateIssue *issue = g_slice_new (GstValidateIssue);
  gchar **area_name = g_strsplit (g_quark_to_string (issue_id), "::", 2);

  g_return_val_if_fail (area_name[0] != NULL && area_name[1] != 0 &&
      area_name[2] == NULL, NULL);

  issue->issue_id = issue_id;
  issue->summary = g_strdup (summary);
  issue->description = g_strdup (description);
  issue->default_level = default_level;
  issue->area = area_name[0];
  issue->name = area_name[1];

  return issue;
}

void
gst_validate_issue_set_default_level (GstValidateIssue * issue,
    GstValidateReportLevel default_level)
{
  GST_INFO ("Setting issue %s::%s default level to %s",
      issue->area, issue->name,
      gst_validate_report_level_get_name (default_level));

  issue->default_level = default_level;
}

static void
gst_validate_issue_free (GstValidateIssue * issue)
{
  g_free (issue->summary);
  g_free (issue->description);

  /* We are using an string array for area and name */
  g_strfreev (&issue->area);
  g_slice_free (GstValidateIssue, issue);
}

void
gst_validate_issue_register (GstValidateIssue * issue)
{
  g_return_if_fail (g_hash_table_lookup (_gst_validate_issues,
          (gpointer) gst_validate_issue_get_id (issue)) == NULL);

  g_hash_table_insert (_gst_validate_issues,
      (gpointer) gst_validate_issue_get_id (issue), issue);
}

#define REGISTER_VALIDATE_ISSUE(lvl,id,sum,desc)			\
  gst_validate_issue_register (gst_validate_issue_new (id, \
						       sum, desc, GST_VALIDATE_REPORT_LEVEL_##lvl))
static void
gst_validate_report_load_issues (void)
{
  g_return_if_fail (_gst_validate_issues == NULL);

  _gst_validate_issues = g_hash_table_new_full (g_direct_hash, g_direct_equal,
      NULL, (GDestroyNotify) gst_validate_issue_free);

  REGISTER_VALIDATE_ISSUE (WARNING, BUFFER_BEFORE_SEGMENT,
      _("buffer was received before a segment"),
      _("in push mode, a segment event must be received before a buffer"));
  REGISTER_VALIDATE_ISSUE (ISSUE, BUFFER_IS_OUT_OF_SEGMENT,
      _("buffer is out of the segment range"),
      _("buffer being pushed is out of the current segment's start-stop "
          " range. Meaning it is going to be discarded downstream without "
          "any use"));
  REGISTER_VALIDATE_ISSUE (WARNING, BUFFER_TIMESTAMP_OUT_OF_RECEIVED_RANGE,
      _("buffer timestamp is out of the received buffer timestamps' range"),
      _("a buffer leaving an element should have its timestamps in the range "
          "of the received buffers timestamps. i.e. If an element received "
          "buffers with timestamps from 0s to 10s, it can't push a buffer with "
          "with a 11s timestamp, because it doesn't have data for that"));
  REGISTER_VALIDATE_ISSUE (WARNING, FIRST_BUFFER_RUNNING_TIME_IS_NOT_ZERO,
      _("first buffer's running time isn't 0"),
      _("the first buffer's received running time is expected to be 0"));
  REGISTER_VALIDATE_ISSUE (WARNING, WRONG_BUFFER,
      _("Received buffer does not correspond to wanted one."),
      _("When checking playback of a file against a MediaInfo file"
          " all buffers coming into the decoders might be checked"
          " and should have the exact expected metadatas and hash of the"
          " content"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, WRONG_FLOW_RETURN,
      _("flow return from pad push doesn't match expected value"),
      _("flow return from a 1:1 sink/src pad element is as simple as "
          "returning what downstream returned. For elements that have multiple "
          "src pads, flow returns should be properly combined"));
  REGISTER_VALIDATE_ISSUE (ISSUE, BUFFER_AFTER_EOS,
      _("buffer was received after EOS"),
      _("a pad shouldn't receive any more buffers after it gets EOS"));

  REGISTER_VALIDATE_ISSUE (ISSUE, CAPS_IS_MISSING_FIELD,
      _("caps is missing a required field for its type"),
      _("some caps types are expected to contain a set of basic fields. "
          "For example, raw video should have 'width', 'height', 'framerate' "
          "and 'pixel-aspect-ratio'"));
  REGISTER_VALIDATE_ISSUE (WARNING, CAPS_FIELD_HAS_BAD_TYPE,
      _("caps field has an unexpected type"),
      _("some common caps fields should always use the same expected types"));
  REGISTER_VALIDATE_ISSUE (WARNING, CAPS_EXPECTED_FIELD_NOT_FOUND,
      _("caps expected field wasn't present"),
      _("a field that should be present in the caps wasn't found. "
          "Fields sets on a sink pad caps should be propagated downstream "
          "when it makes sense to do so"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, GET_CAPS_NOT_PROXYING_FIELDS,
      _("getcaps function isn't proxying downstream fields correctly"),
      _("elements should set downstream caps restrictions on its caps when "
          "replying upstream's getcaps queries to avoid upstream sending data"
          " in an unsupported format"));
  REGISTER_VALIDATE_ISSUE (CRITICAL, CAPS_FIELD_UNEXPECTED_VALUE,
      _("a field in caps has an unexpected value"),
      _("fields set on a sink pad should be propagated downstream via "
          "set caps"));

  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_NEWSEGMENT_NOT_PUSHED,
      _("new segment event wasn't propagated downstream"),
      _("segments received from upstream should be pushed downstream"));
  REGISTER_VALIDATE_ISSUE (WARNING, SERIALIZED_EVENT_WASNT_PUSHED_IN_TIME,
      _("a serialized event received should be pushed in the same 'time' "
          "as it was received"),
      _("serialized events should be pushed in the same order they are "
          "received and serialized with buffers. If an event is received after"
          " a buffer with timestamp end 'X', it should be pushed right after "
          "buffers with timestamp end 'X'"));
  REGISTER_VALIDATE_ISSUE (ISSUE, EVENT_HAS_WRONG_SEQNUM,
      _("events that are part of the same pipeline 'operation' should "
          "have the same seqnum"),
      _("when events/messages are created from another event/message, "
          "they should have their seqnums set to the original event/message "
          "seqnum"));
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_SERIALIZED_OUT_OF_ORDER,
      _("a serialized event received should be pushed in the same order "
          "as it was received"),
      _("serialized events should be pushed in the same order they are "
          "received."));
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_NEW_SEGMENT_MISMATCH,
      _("a new segment event has different value than the received one"),
      _("when receiving a new segment, an element should push an equivalent"
          "segment downstream"));
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_FLUSH_START_UNEXPECTED,
      _("received an unexpected flush start event"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_FLUSH_STOP_UNEXPECTED,
      _("received an unexpected flush stop event"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_CAPS_DUPLICATE,
      _("received the same caps twice"), NULL);

  REGISTER_VALIDATE_ISSUE (CRITICAL, EVENT_SEEK_NOT_HANDLED,
      _("seek event wasn't handled"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, EVENT_SEEK_RESULT_POSITION_WRONG,
      _("position after a seek is wrong"), NULL);

  REGISTER_VALIDATE_ISSUE (WARNING, EVENT_EOS_WITHOUT_SEGMENT,
      _("EOS received without segment event before"),
      _("A segment event should always be sent before data flow"
          " EOS being some kind of data flow, there is no exception"
          " in that regard"));

  REGISTER_VALIDATE_ISSUE (CRITICAL, STATE_CHANGE_FAILURE,
      _("state change failed"), NULL);

  REGISTER_VALIDATE_ISSUE (WARNING, FILE_SIZE_INCORRECT,
      _("resulting file size wasn't within the expected values"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_DURATION_INCORRECT,
      _("resulting file duration wasn't within the expected values"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_SEEKABLE_INCORRECT,
      _("resulting file wasn't seekable or not seekable as expected"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, FILE_PROFILE_INCORRECT,
      _("resulting file stream profiles didn't match expected values"), NULL);
  REGISTER_VALIDATE_ISSUE (ISSUE, FILE_TAG_DETECTION_INCORRECT,
      _("detected tags are different than expected ones"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, FILE_NO_STREAM_ID,
      _("the discoverer found a stream that had no stream ID"), NULL);


  REGISTER_VALIDATE_ISSUE (CRITICAL, ALLOCATION_FAILURE,
      _("a memory allocation failed during Validate run"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, MISSING_PLUGIN,
      _("a gstreamer plugin is missing and prevented Validate from running"),
      NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, WARNING_ON_BUS,
      _("We got a WARNING message on the bus"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, ERROR_ON_BUS,
      _("We got an ERROR message on the bus"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, QUERY_POSITION_SUPERIOR_DURATION,
      _("Query position reported a value superior than what query duration "
          "returned"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, QUERY_POSITION_OUT_OF_SEGMENT,
      _("Query position reported a value outside of the current expected "
          "segment"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, SCENARIO_NOT_ENDED,
      _("All the actions were not executed before the program stoped"), NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, SCENARIO_ACTION_EXECUTION_ERROR,
      _("The execution of an action did not properly happen"), NULL);
  REGISTER_VALIDATE_ISSUE (ISSUE, SCENARIO_ACTION_EXECUTION_ISSUE,
      _("An issue happend during the execution of a scenario"), NULL);
  REGISTER_VALIDATE_ISSUE (WARNING, G_LOG_WARNING, _("We got a g_log warning"),
      NULL);
  REGISTER_VALIDATE_ISSUE (CRITICAL, G_LOG_CRITICAL,
      _("We got a g_log critical issue"), NULL);
  REGISTER_VALIDATE_ISSUE (ISSUE, G_LOG_ISSUE, _("We got a g_log issue"), NULL);
}

static void
error_cb (GstBus * bus, GstMessage * msg, gpointer unused)
{
  GError *err;
  gchar *debug_info;

  gst_message_parse_error (msg, &err, &debug_info);
  GST_ERROR ("Error received from element %s: %s",
      GST_OBJECT_NAME (msg->src), err->message);
  g_clear_error (&err);
  g_free (debug_info);
}

static gboolean
_create_pipeline_from_uri (const gchar * uri, PipelineLog * plog)
{
  GstCaps *caps;
  GError *err = NULL;
  GstBus *bus;
  GSource *bus_source;
  GMainContext *mcontext;
  GstElement *sink, *src, *pipeline;

  if (!gst_uri_is_valid (uri)) {
    if (g_strcmp0 (uri, "stderr") == 0) {
      plog->is_std = TRUE;
      plog->file = stderr;

      return TRUE;
    } else if (g_strcmp0 (uri, "stdout") == 0) {
      plog->is_std = TRUE;
      plog->file = stdout;

      return TRUE;
    }

    sink = gst_element_factory_make ("filesink", NULL);
    g_object_set (sink, "location", uri, NULL);
  } else {
    sink = gst_element_make_from_uri (GST_URI_SINK, uri, NULL, &err);
  }

  if (sink == NULL) {
    GST_ERROR ("Could not create a sink for %s (error: %s)", uri,
        err ? err->message : "None");
    return FALSE;
  }

  g_object_set (sink, "async", FALSE, "qos", FALSE, "sync", FALSE, NULL);
  src = gst_element_factory_make ("appsrc", NULL);
  caps = gst_caps_new_simple ("raw/x-text", NULL, NULL);
  g_object_set (src, "caps", caps, NULL);
  pipeline = gst_pipeline_new (NULL);
  bus = gst_element_get_bus (pipeline);
  mcontext = g_main_context_get_thread_default ();
  if (mcontext == NULL) {
    GST_DEBUG ("Using main context as no one found for the" "thread");
    mcontext = g_main_context_default ();
  }

  bus_source = gst_bus_create_watch (bus);
  g_source_set_callback (bus_source, (GSourceFunc) gst_bus_async_signal_func,
      NULL, NULL);
  g_source_attach (bus_source, mcontext);
  g_source_unref (bus_source);
  g_signal_connect (G_OBJECT (bus), "message::error", (GCallback) error_cb,
      NULL);
  gst_object_unref (bus);

  gst_bin_add_many (GST_BIN (pipeline), src, sink, NULL);

  gst_element_link (src, sink);

  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  plog->pipeline = pipeline;
  plog->src = src;

  return TRUE;
}

static void
gst_validate_default_print (GString * string)
{
  gint i;
  gboolean clean_string = TRUE;
  GstBuffer *buffer = NULL;

  for (i = 0; i < log_pipelines->len; i++) {
    GstFlowReturn res;
    PipelineLog *plog = &g_array_index (log_pipelines,
        PipelineLog, i);

    if (plog->is_std) {
      fprintf (plog->file, "%s", string->str);
      fflush (plog->file);

      continue;
    }

    if (buffer == NULL) {
      buffer = gst_buffer_new_wrapped (g_strdup (string->str),
          strlen (string->str) * sizeof (gchar));
      clean_string = FALSE;
    } else {
      gst_buffer_ref (buffer);
    }

    g_signal_emit_by_name (plog->src, "push-buffer", buffer, &res);
  }

  g_string_free (string, clean_string);
}



void
gst_validate_report_init (void)
{
  const gchar *var, *file_env;
  const GDebugKey keys[] = {
    {"fatal_criticals", GST_VALIDATE_FATAL_CRITICALS},
    {"fatal_warnings", GST_VALIDATE_FATAL_WARNINGS},
    {"fatal_issues", GST_VALIDATE_FATAL_ISSUES},
    {"print_issues", GST_VALIDATE_PRINT_ISSUES},
    {"print_warnings", GST_VALIDATE_PRINT_WARNINGS},
    {"print_criticals", GST_VALIDATE_PRINT_CRITICALS}
  };

  GST_DEBUG_CATEGORY_INIT (gst_validate_report_debug, "gstvalidatereport",
      GST_DEBUG_FG_YELLOW, "Gst validate reporting");

  if (_gst_validate_report_start_time == 0) {
    _gst_validate_report_start_time = gst_util_get_timestamp ();

    /* init the debug flags */
    var = g_getenv ("GST_VALIDATE");
    if (var && strlen (var) > 0) {
      _gst_validate_flags =
          g_parse_debug_string (var, keys, G_N_ELEMENTS (keys));
    }

    gst_validate_report_load_issues ();
  }

  gst_validate_report_add_print_func (gst_validate_default_print);

  file_env = g_getenv ("GST_VALIDATE_FILE");
  log_pipelines = g_array_new (TRUE, TRUE, sizeof (PipelineLog));
  if (file_env != NULL && *file_env != '\0') {
    guint i;
    gchar **wanted_files;
    wanted_files = g_strsplit (file_env, "::", 0);

    /* FIXME: Make sure it is freed in the deinit function when that is
     * implemented */
    for (i = 0; i < g_strv_length (wanted_files); i++) {
      PipelineLog plog = { 0, 0, 0, 0};

      if (_create_pipeline_from_uri (wanted_files[i], &plog))
        g_array_append_val (log_pipelines, plog);
    }
  } else {
    PipelineLog plog = { 0, 0, 0, 0};

    if (_create_pipeline_from_uri ("stdout", &plog))
      g_array_append_val (log_pipelines, plog);
  }

#ifndef GST_DISABLE_GST_DEBUG
  if (!newline_regex)
    newline_regex =
        g_regex_new ("\n", G_REGEX_OPTIMIZE | G_REGEX_MULTILINE, 0, NULL);
#endif

}

GstValidateIssue *
gst_validate_issue_from_id (GstValidateIssueId issue_id)
{
  return g_hash_table_lookup (_gst_validate_issues, (gpointer) issue_id);
}

/* TODO how are these functions going to work with extensions */
const gchar *
gst_validate_report_level_get_name (GstValidateReportLevel level)
{
  switch (level) {
    case GST_VALIDATE_REPORT_LEVEL_CRITICAL:
      return "critical";
    case GST_VALIDATE_REPORT_LEVEL_WARNING:
      return "warning";
    case GST_VALIDATE_REPORT_LEVEL_ISSUE:
      return "issue";
    case GST_VALIDATE_REPORT_LEVEL_IGNORE:
      return "ignore";
    default:
      return "unknown";
  }
}

GstValidateReportLevel
gst_validate_report_level_from_name (const gchar * issue_name)
{
  if (g_strcmp0 (issue_name, "critical") == 0)
    return GST_VALIDATE_REPORT_LEVEL_CRITICAL;

  else if (g_strcmp0 (issue_name, "warning") == 0)
    return GST_VALIDATE_REPORT_LEVEL_WARNING;

  else if (g_strcmp0 (issue_name, "issue") == 0)
    return GST_VALIDATE_REPORT_LEVEL_ISSUE;

  else if (g_strcmp0 (issue_name, "ignore") == 0)
    return GST_VALIDATE_REPORT_LEVEL_IGNORE;

  return GST_VALIDATE_REPORT_LEVEL_UNKNOWN;
}

gboolean
gst_validate_report_should_print (GstValidateReport * report)
{
  if ((!(_gst_validate_flags & GST_VALIDATE_PRINT_ISSUES) &&
          !(_gst_validate_flags & GST_VALIDATE_PRINT_WARNINGS) &&
          !(_gst_validate_flags & GST_VALIDATE_PRINT_CRITICALS))) {
    return TRUE;
  }

  if ((report->level <= GST_VALIDATE_REPORT_LEVEL_ISSUE &&
          _gst_validate_flags & GST_VALIDATE_PRINT_ISSUES) ||
      (report->level <= GST_VALIDATE_REPORT_LEVEL_WARNING &&
          _gst_validate_flags & GST_VALIDATE_PRINT_WARNINGS) ||
      (report->level <= GST_VALIDATE_REPORT_LEVEL_CRITICAL &&
          _gst_validate_flags & GST_VALIDATE_PRINT_CRITICALS)) {

    return TRUE;
  }

  return FALSE;
}

gboolean
gst_validate_report_check_abort (GstValidateReport * report)
{
  if ((report->level <= GST_VALIDATE_REPORT_LEVEL_ISSUE &&
          _gst_validate_flags & GST_VALIDATE_FATAL_ISSUES) ||
      (report->level <= GST_VALIDATE_REPORT_LEVEL_WARNING &&
          _gst_validate_flags & GST_VALIDATE_FATAL_WARNINGS) ||
      (report->level <= GST_VALIDATE_REPORT_LEVEL_CRITICAL &&
          _gst_validate_flags & GST_VALIDATE_FATAL_CRITICALS)) {

    return TRUE;
  }

  return FALSE;
}

GstValidateIssueId
gst_validate_report_get_issue_id (GstValidateReport * report)
{
  return gst_validate_issue_get_id (report->issue);
}

GstValidateReport *
gst_validate_report_new (GstValidateIssue * issue,
    GstValidateReporter * reporter, const gchar * message)
{
  GstValidateReport *report = g_slice_new0 (GstValidateReport);

  report->refcount = 1;
  report->issue = issue;
  report->reporter = reporter;  /* TODO should we ref? */
  report->message = g_strdup (message);
  g_mutex_init (&report->shadow_reports_lock);
  report->timestamp =
      gst_util_get_timestamp () - _gst_validate_report_start_time;
  report->level = issue->default_level;
  report->reporting_level = GST_VALIDATE_SHOW_UNKNOWN;

  return report;
}

void
gst_validate_report_unref (GstValidateReport * report)
{
  g_return_if_fail (report != NULL);

  if (G_UNLIKELY (g_atomic_int_dec_and_test (&report->refcount))) {
    g_free (report->message);
    g_list_free_full (report->shadow_reports,
        (GDestroyNotify) gst_validate_report_unref);
    g_list_free_full (report->repeated_reports,
        (GDestroyNotify) gst_validate_report_unref);
    g_slice_free (GstValidateReport, report);
    g_mutex_clear (&report->shadow_reports_lock);
  }
}

GstValidateReport *
gst_validate_report_ref (GstValidateReport * report)
{
  g_return_val_if_fail (report != NULL, NULL);

  g_atomic_int_inc (&report->refcount);

  return report;
}

void
gst_validate_printf (gpointer source, const gchar * format, ...)
{
  va_list var_args;

  va_start (var_args, format);
  gst_validate_printf_valist (source, format, var_args);
  va_end (var_args);
}

static void
_call_print_funcs (GString * string)
{
  GList *tmp;

  for (tmp = print_funcs; tmp; tmp = tmp->next) {
    ((GstValidatePrintFunc) tmp->data) (g_string_new (string->str));
  }

  g_string_free (string, TRUE);
}

void
gst_validate_printf_valist (gpointer source, const gchar * format, va_list args)
{
  GString *string = g_string_new (NULL);

  if (source) {
    if (*(GType *) source == GST_TYPE_VALIDATE_ACTION) {
      GstValidateAction *action = (GstValidateAction *) source;

      g_string_printf (string,
          "\n(Executing action: %s, number: %u at position: %" GST_TIME_FORMAT
          " repeat: %i) | ", g_strcmp0 (action->name,
              "") == 0 ? "Unnamed" : action->name, action->action_number,
          GST_TIME_ARGS (action->playback_time), action->repeat);

    } else if (*(GType *) source == GST_TYPE_VALIDATE_ACTION_TYPE) {
      gint i;
      gchar *desc, *tmp;

      GstValidateActionType *type = GST_VALIDATE_ACTION_TYPE (source);

      g_string_printf (string, "\nAction type:");
      g_string_append_printf (string, "\n  Name: %s", type->name);
      g_string_append_printf (string, "\n  Implementer namespace: %s",
          type->implementer_namespace);

      if (type->is_config)
        g_string_append_printf (string,
            "\n    Is config action (meaning it will be executing right "
            "at the begining of the execution of the pipeline)");

      tmp = g_strdup_printf ("\n    ");
      desc =
          g_regex_replace (newline_regex, type->description, -1, 0, tmp, 0,
          NULL);
      g_string_append_printf (string, "\n\n  Description: \n    %s", desc);
      g_free (desc);
      g_free (tmp);

      if (type->parameters) {
        g_string_append_printf (string, "\n\n  Parametters:");

        for (i = 0; type->parameters[i].name; i++) {
          gint nw = 0;
          gchar *param_head =
              g_strdup_printf ("    %s", type->parameters[i].name);
          gchar *tmp_head = g_strdup_printf ("\n %-30s : %s",
              param_head, "something");


          while (tmp_head[nw] != ':')
            nw++;

          g_free (tmp_head);

          tmp = g_strdup_printf ("\n%*s", nw + 1, " ");

          if (g_strcmp0 (type->parameters[i].description, "")) {
            desc =
                g_regex_replace (newline_regex, type->parameters[i].description,
                -1, 0, tmp, 0, NULL);
          } else {
            desc = g_strdup_printf ("No description");
          }

          g_string_append_printf (string, "\n %-30s : %s", param_head, desc);
          g_free (desc);

          if (type->parameters[i].possible_variables) {
            gchar *tmp1 = g_strdup_printf ("\n%*s", nw + 4, " ");
            desc =
                g_regex_replace (newline_regex,
                type->parameters[i].possible_variables, -1, 0, tmp1, 0, NULL);
            g_string_append_printf (string, "%sPossible variables:%s%s", tmp,
                tmp1, desc);

            g_free (tmp1);
          }

          if (type->parameters[i].types) {
            gchar *tmp1 = g_strdup_printf ("\n%*s", nw + 4, " ");
            desc =
                g_regex_replace (newline_regex,
                type->parameters[i].types, -1, 0, tmp1, 0, NULL);
            g_string_append_printf (string, "%sPossible types:%s%s", tmp,
                tmp1, desc);

            g_free (tmp1);
          }

          if (!type->parameters[i].mandatory) {
            g_string_append_printf (string, "%sDefault: %s", tmp,
                type->parameters[i].def);
          }

          g_string_append_printf (string, "%s%s", tmp,
              type->parameters[i].mandatory ? "Mandatory." : "Optional.");

          g_free (tmp);
          g_free (param_head);

        }
      } else {
        g_string_append_printf (string, "\n\n  No Parameters");

      }
    } else if (GST_IS_OBJECT (source)) {
      g_string_printf (string, "\n%s --> ", GST_OBJECT_NAME (source));
    } else if (G_IS_OBJECT (source)) {
      g_string_printf (string, "\n<%s@%p> --> ", G_OBJECT_TYPE_NAME (source),
          source);
    }
  }

  g_string_append_vprintf (string, format, args);

  if (!newline_regex)
    newline_regex =
        g_regex_new ("\n", G_REGEX_OPTIMIZE | G_REGEX_MULTILINE, 0, NULL);

#ifndef GST_DISABLE_GST_DEBUG
  {
    gchar *str;

    str = g_regex_replace (newline_regex, string->str, string->len, 0,
        "", 0, NULL);

    if (source)
      GST_INFO ("%s", str);
    else
      GST_DEBUG ("%s", str);

    g_free (str);
  }
#endif

  _call_print_funcs (string);
}

gboolean
gst_validate_report_set_master_report (GstValidateReport * report,
    GstValidateReport * master_report)
{
  GList *tmp;
  gboolean add_shadow_report = TRUE;

  if (master_report->reporting_level >= GST_VALIDATE_SHOW_MONITOR)
    return FALSE;

  report->master_report = master_report;

  GST_VALIDATE_REPORT_SHADOW_REPORTS_LOCK (master_report);
  for (tmp = master_report->shadow_reports; tmp; tmp = tmp->next) {
    GstValidateReport *shadow_report = (GstValidateReport *) tmp->data;
    if (report->reporter == shadow_report->reporter) {
      add_shadow_report = FALSE;
      break;
    }
  }
  if (add_shadow_report)
    master_report->shadow_reports =
        g_list_append (master_report->shadow_reports,
        gst_validate_report_ref (report));
  GST_VALIDATE_REPORT_SHADOW_REPORTS_UNLOCK (master_report);

  return TRUE;
}

void
gst_validate_report_append_level_to_string (GstValidateReport * report, GString *string)
{
  g_string_append_printf (string, "%10s : %s\n",
      gst_validate_report_level_get_name (report->level),
      report->issue->summary);
}

void
gst_validate_report_append_detected_on_to_string (GstValidateReport * report, GString *string)
{
  GList *tmp;

  g_string_append_printf (string, "%*s Detected on <%s",
      12, "", gst_validate_reporter_get_name (report->reporter));
  for (tmp = report->shadow_reports; tmp; tmp = tmp->next) {
    GstValidateReport *shadow_report = (GstValidateReport *) tmp->data;
    g_string_append_printf (string, ", %s",
        gst_validate_reporter_get_name (shadow_report->reporter));
  }
  g_string_append_printf (string, ">\n");
}

void
gst_validate_report_append_details_to_string (GstValidateReport * report, GString *string)
{
  if (report->message)
    g_string_append_printf (string, "%*s Details : %s\n", 12, "", report->message);
}

void
gst_validate_report_append_description_to_string (GstValidateReport * report, GString *string)
{
  if (report->issue->description)
    g_string_append_printf (string, "%*s Description : %s\n", 12, "",
        report->issue->description);
}

void
gst_validate_report_printf (GstValidateReport * report)
{
  GList *tmp;
  GString *string = g_string_new (NULL);

  gst_validate_report_append_level_to_string (report, string);
  gst_validate_report_append_detected_on_to_string (report, string);
  gst_validate_report_append_details_to_string (report, string);

  for (tmp = report->repeated_reports; tmp; tmp = tmp->next) {
    gst_validate_report_append_details_to_string (report, string);
  }

  gst_validate_report_append_description_to_string (report, string);
  g_string_append_printf (string, "\n");

  _call_print_funcs (string);
}

void
gst_validate_report_set_reporting_level (GstValidateReport * report,
    GstValidateReportingDetails level)
{
  report->reporting_level = level;
}

void
gst_validate_report_add_repeated_report (GstValidateReport * report,
    GstValidateReport * repeated_report)
{
  report->repeated_reports =
      g_list_append (report->repeated_reports,
      gst_validate_report_ref (repeated_report));
}

void
gst_validate_report_deinit (void)
{
  gint i;

  for (i = 0; i < log_pipelines->len; i++) {
    PipelineLog *plog = &g_array_index (log_pipelines,
        PipelineLog, i);

    if (plog->is_std)
      continue;

    gst_element_set_state (plog->pipeline, GST_STATE_NULL);
    gst_element_get_state (plog->pipeline, NULL, NULL, GST_CLOCK_TIME_NONE);

    gst_object_unref (plog->pipeline);
  }

  g_array_unref (log_pipelines);
}

void
gst_validate_report_add_print_func (GstValidatePrintFunc func)
{
  print_funcs = g_list_prepend (print_funcs, func);
}
