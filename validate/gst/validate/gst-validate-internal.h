/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * validate.c - Validate generic functions
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

#ifndef __GST_VALIDATE_INTERNAL_H__
#define __GST_VALIDATE_INTERNAL_H__

#include <gst/gst.h>

#include "gst-validate-enums.h"
#include "gst-validate-types.h"
#include "gst-validate-report.h"
#include "gst-validate-override.h"
#include "gst-validate-scenario.h"
#include "gst-validate-reporter.h"

GST_DEBUG_CATEGORY_EXTERN (gstvalidate_debug);
#ifndef GST_CAT_DEFAULT
#define GST_CAT_DEFAULT gstvalidate_debug
#endif /* ifndef GST_CAT_DEFAULT */
#define I G_GNUC_INTERNAL

extern GRegex *newline_regex;

/****************************************************************
 *          GstValidateMonitor internal symbols                 *
 ****************************************************************/

#define GST_VALIDATE_MONITOR_GET_OBJECT(m) (GST_VALIDATE_MONITOR_CAST (m)->target)
#define GST_VALIDATE_MONITOR_GET_RUNNER(m) (gst_validate_reporter_get_runner (GST_VALIDATE_REPORTER_CAST (m)))
#define GST_VALIDATE_MONITOR_GET_PARENT(m) (GST_VALIDATE_MONITOR_CAST (m)->parent)

#define GST_VALIDATE_MONITOR_LOCK(m)			\
  G_STMT_START {					\
  GST_LOG_OBJECT (m, "About to lock %p", &GST_VALIDATE_MONITOR_CAST(m)->mutex); \
  (g_mutex_lock (&GST_VALIDATE_MONITOR_CAST(m)->mutex));		\
  GST_LOG_OBJECT (m, "Acquired lock %p", &GST_VALIDATE_MONITOR_CAST(m)->mutex); \
  } G_STMT_END

#define GST_VALIDATE_MONITOR_UNLOCK(m)				\
  G_STMT_START {						\
  GST_LOG_OBJECT (m, "About to unlock %p", &GST_VALIDATE_MONITOR_CAST(m)->mutex); \
  (g_mutex_unlock (&GST_VALIDATE_MONITOR_CAST(m)->mutex));		\
  GST_LOG_OBJECT (m, "unlocked %p", &GST_VALIDATE_MONITOR_CAST(m)->mutex); \
  } G_STMT_END

#define GST_VALIDATE_MONITOR_OVERRIDES_LOCK(m) g_mutex_lock (&GST_VALIDATE_MONITOR_CAST (m)->overrides_mutex)
#define GST_VALIDATE_MONITOR_OVERRIDES_UNLOCK(m) g_mutex_unlock (&GST_VALIDATE_MONITOR_CAST (m)->overrides_mutex)
#define GST_VALIDATE_MONITOR_OVERRIDES(m) (GST_VALIDATE_MONITOR_CAST (m)->overrides)

/* #else TODO Implemen no variadic macros, use inline,
 * Problem being:
 *     GST_VALIDATE_REPORT_LEVEL_ ## status
 *     GST_VALIDATE_AREA_ ## area ## _ ## subarea
 */

/**
 * GstValidateMonitor:
 *
 * GStreamer Validate Monitor class.
 *
 * Class that wraps a #GObject for Validate checks
 */
struct _GstValidateMonitor {
  GObject 	 object;

  GstObject     *target;
  GMutex         mutex;
  gchar         *target_name;

  GstValidateMonitor  *parent;

  GMutex        overrides_mutex;
  GQueue        overrides;
  GstMediaDescriptor *media_descriptor;

  GstValidateReportingDetails level;

  /*< private >*/
  GHashTable *reports;
};

I void            gst_validate_monitor_attach_override  (GstValidateMonitor * monitor,
                                                         GstValidateOverride * override);

I GstElement *    gst_validate_monitor_get_element (GstValidateMonitor * monitor);
I const gchar *   gst_validate_monitor_get_element_name (GstValidateMonitor * monitor);

/**
 * GstValidateMonitorClass:
 * @parent_class: parent
 *
 * GStreamer Validate Monitor object class.
 */
struct _GstValidateMonitorClass {
  GObjectClass	parent_class;

  gboolean (* setup) (GstValidateMonitor * monitor);
  GstElement *(* get_element) (GstValidateMonitor * monitor);
  void (*set_media_descriptor) (GstValidateMonitor * monitor,
          GstMediaDescriptor * media_descriptor);
};

/****************************************************************
 *          GstValidateElementMonitor internal symbols          *
 ****************************************************************/

/**
 * GstValidateElementMonitor:
 *
 * GStreamer Validate ElementMonitor class.
 *
 * Class that wraps a #GstElement for Validate checks
 */
struct _GstValidateElementMonitor {
  GstValidateMonitor 	 parent;

  /*< private >*/
  gulong         pad_added_id;
  GList         *pad_monitors;

  gboolean       is_decoder;
  gboolean       is_encoder;
  gboolean       is_demuxer;
};

/**
 * GstValidateElementMonitorClass:
 * @parent_class: parent
 *
 * GStreamer Validate ElementMonitor object class.
 */
struct _GstValidateElementMonitorClass {
  GstValidateMonitorClass	parent_class;
};

/****************************************************************
 *          GstValidateBinMonitor internal symbols              *
 ****************************************************************/
/**
 * GstValidateBinMonitor:
 *
 * GStreamer Validate BinMonitor class.
 *
 * Class that wraps a #GstBin for Validate checks
 */
struct _GstValidateBinMonitor {
  GstValidateElementMonitor parent;

  /*< private >*/
  /*  Internal */
  GList *element_monitors;

  GstValidateScenario *scenario;

  gulong element_added_id;
  gboolean stateless;
};

/**
 * GstValidateBinMonitorClass:
 * @parent_class: parent
 *
 * GStreamer Validate BinMonitor object class.
 */
struct _GstValidateBinMonitorClass {
  GstValidateElementMonitorClass parent_class;
};

/****************************************************************
 *          GstValidatePadMonitor internal symbols              *
 ****************************************************************/
/**
 * GstValidatePadMonitor:
 *
 * GStreamer Validate PadMonitor class.
 *
 * Class that wraps a #GstPad for Validate checks
 */
struct _GstValidatePadMonitor {
  GstValidateMonitor 	 parent;

  GstValidateElementMonitor *element_monitor;

  gboolean       setup;
  GstPad        *pad;

  GstPadChainFunction chain_func;
  GstPadEventFunction event_func;
  GstPadGetRangeFunction getrange_func;
  GstPadQueryFunction query_func;
  GstPadActivateModeFunction activatemode_func;

  gulong pad_probe_id;

  /*< private >*/
  /* Last caps pushed/received */
  GstCaps *last_caps;
  gboolean caps_is_audio;
  gboolean caps_is_video;
  gboolean caps_is_raw;

  /* FIXME : Let's migrate all those booleans into a 32 (or 64) bit flag */
  gboolean first_buffer;

  gboolean has_segment;
  gboolean is_eos;

  gboolean pending_flush_stop;
  guint32 pending_flush_stop_seqnum;
  guint32 pending_flush_start_seqnum;
  guint32 pending_newsegment_seqnum;
  guint32 pending_eos_seqnum;

  GstEvent *expected_segment;
  GPtrArray *serialized_events;
  GList *expired_events;

  GstStructure *pending_setcaps_fields;

  /* tracked data */
  GstSegment segment;
  GstClockTime current_timestamp;
  GstClockTime current_duration;

  GstFlowReturn last_flow_return;

  /* Stores the timestamp range of data that has flown through
   * this pad by using TIMESTAMP and TIMESTAMP+DURATION from
   * incomming buffers. Every time a buffer is pushed, this range
   * is extended.
   *
   * When a buffer is pushed, the timestamp range is checked against
   * the outgoing timestamp to check it is in the received boundaries.
   */
  GstClockTime timestamp_range_start;
  GstClockTime timestamp_range_end;

  /* GstMediaCheck related fields */
  GList *all_bufs;
  /* The GstBuffer that should arrive next in a GList */
  GList *current_buf;
  gboolean check_buffers;
};

/**
 * GstValidatePadMonitorClass:
 * @parent_class: parent
 *
 * GStreamer Validate PadMonitor object class.
 */
struct _GstValidatePadMonitorClass {
  GstValidateMonitorClass	parent_class;
};

/****************************************************************
 *          GstValidateScenario internal symbols                *
 ****************************************************************/
typedef struct _GstValidateActionType      GstValidateActionType;

#define IS_CONFIG_ACTION_TYPE(type) (((type) & GST_VALIDATE_ACTION_TYPE_CONFIG) || ((type) == TRUE))

struct _GstValidateActionType
{
  GstMiniObject          mini_object;

  gchar *name;
  gchar *implementer_namespace;

  GstValidateExecuteAction execute;

  GstValidateActionParameter *parameters;

  gchar *description;
  GstValidateActionTypeFlags flags;

  gpointer _gst_reserved[GST_PADDING_LARGE];
};


GST_EXPORT GType _gst_validate_action_type_type;

I void init_scenarios (void);

/****************************************************************
 *          GstValidateRunner internal symbols                  *
 ****************************************************************/
void                        gst_validate_runner_add_report                   (GstValidateRunner * runner,
                                                                              GstValidateReport * report);
GstValidateReportingDetails gst_validate_runner_get_default_reporting_level  (GstValidateRunner *runner);
GstValidateReportingDetails gst_validate_runner_get_reporting_level_for_name (GstValidateRunner *runner,
                                                                              const gchar *name);

/****************************************************************
 *          GstValidateReporter internal symbols                *
 ****************************************************************/

I void gst_validate_reporter_set_name                  (GstValidateReporter * reporter,
                                                        gchar * name);
I const gchar * gst_validate_reporter_get_name         (GstValidateReporter * reporter);
I GstValidateRunner * gst_validate_reporter_get_runner (GstValidateReporter *reporter);
I void gst_validate_reporter_init                      (GstValidateReporter * reporter,
                                                        const gchar *name);
I void gst_validate_report_valist                      (GstValidateReporter * reporter,
                                                        GstValidateIssueId issue_id,
                                                        const gchar * format,
                                                        va_list var_args);
I void gst_validate_reporter_set_runner                (GstValidateReporter * reporter,
                                                        GstValidateRunner *runner);
I GstValidateReport * gst_validate_reporter_get_report (GstValidateReporter *reporter,
                                                        GstValidateIssueId issue_id);
GList *               gst_validate_runner_get_reports  (GstValidateRunner * runner);

/* Accessible from outside for the testsuite  */
GList * gst_validate_reporter_get_reports              (GstValidateReporter * reporter);
GstValidateReportingDetails
gst_validate_reporter_get_reporting_level              (GstValidateReporter *reporter);

/****************************************************************
 *          GstValidateReport internal symbols                  *
 ****************************************************************/

#define _QUARK g_quark_from_static_string

#define BUFFER_BEFORE_SEGMENT                    _QUARK("buffer::before-segment")
#define BUFFER_IS_OUT_OF_SEGMENT                 _QUARK("buffer::is-out-of-segment")
#define BUFFER_TIMESTAMP_OUT_OF_RECEIVED_RANGE   _QUARK("buffer::timestamp-out-of-received-range")
#define WRONG_FLOW_RETURN                        _QUARK("buffer::wrong-flow-return")
#define BUFFER_AFTER_EOS                         _QUARK("buffer::after-eos")
#define WRONG_BUFFER                             _QUARK("buffer::not-expected-one")

#define CAPS_IS_MISSING_FIELD                    _QUARK("caps::is-missing-field")
#define CAPS_FIELD_HAS_BAD_TYPE                  _QUARK("caps::field-has-bad-type")
#define CAPS_EXPECTED_FIELD_NOT_FOUND            _QUARK("caps::expected-field-not-found")
#define GET_CAPS_NOT_PROXYING_FIELDS             _QUARK("caps::not-proxying-fields")
#define CAPS_FIELD_UNEXPECTED_VALUE              _QUARK("caps::field-unexpected-value")

#define EVENT_NEWSEGMENT_NOT_PUSHED              _QUARK("event::newsegment-not-pushed")
#define SERIALIZED_EVENT_WASNT_PUSHED_IN_TIME    _QUARK("event::serialized-event-wasnt-pushed-in-time")

#define EOS_HAS_WRONG_SEQNUM                    _QUARK("event::eos-has-wrong-seqnum")
#define FLUSH_START_HAS_WRONG_SEQNUM            _QUARK("event::flush-start-has-wrong-seqnum")
#define FLUSH_STOP_HAS_WRONG_SEQNUM             _QUARK("event::flush-stop-has-wrong-seqnum")
#define SEGMENT_HAS_WRONG_SEQNUM                _QUARK("event::segment-has-wrong-seqnum")


#define EVENT_SERIALIZED_OUT_OF_ORDER            _QUARK("event::serialized-out-of-order")
#define EVENT_NEW_SEGMENT_MISMATCH               _QUARK("event::segment-mismatch")
#define EVENT_FLUSH_START_UNEXPECTED             _QUARK("event::flush-start-unexpected")
#define EVENT_FLUSH_STOP_UNEXPECTED              _QUARK("event::flush-stop-unexpected")
#define EVENT_CAPS_DUPLICATE                     _QUARK("event::caps-duplicate")
#define EVENT_SEEK_NOT_HANDLED                   _QUARK("event::seek-not-handled")
#define EVENT_SEEK_RESULT_POSITION_WRONG         _QUARK("event::seek-result-position-wrong")
#define EVENT_EOS_WITHOUT_SEGMENT                _QUARK("event::eos-without-segment")

#define STATE_CHANGE_FAILURE                     _QUARK("state::change-failure")

#define FILE_NO_STREAM_ID                        _QUARK("file-checking::no-stream-id")
#define FILE_TAG_DETECTION_INCORRECT             _QUARK("file-checking::tag-detection-incorrect")
#define FILE_SIZE_INCORRECT                      _QUARK("file-checking::size-incorrect")
#define FILE_DURATION_INCORRECT                  _QUARK("file-checking::duration-incorrect")
#define FILE_SEEKABLE_INCORRECT                  _QUARK("file-checking::seekable-incorrect")
#define FILE_PROFILE_INCORRECT                   _QUARK("file-checking::profile-incorrect")

#define ALLOCATION_FAILURE                       _QUARK("runtime::allocation-failure")
#define MISSING_PLUGIN                           _QUARK("runtime::missing-plugin")
#define WARNING_ON_BUS                           _QUARK("runtime::warning-on-bus")
#define ERROR_ON_BUS                             _QUARK("runtime::error-on-bus")

#define QUERY_POSITION_SUPERIOR_DURATION         _QUARK("query::position-superior-duration")
#define QUERY_POSITION_OUT_OF_SEGMENT            _QUARK("query::position-out-of-segment")

#define SCENARIO_NOT_ENDED                       _QUARK("scenario::not-ended")
#define SCENARIO_ACTION_EXECUTION_ERROR          _QUARK("scenario::execution-error")
#define SCENARIO_ACTION_EXECUTION_ISSUE          _QUARK("scenario::execution-issue")

#define G_LOG_ISSUE                              _QUARK("g-log::issue")
#define G_LOG_WARNING                            _QUARK("g-log::warning")
#define G_LOG_CRITICAL                           _QUARK("g-log::critical")

struct _GstValidateIssue {
  GstValidateIssueId issue_id;

  /* Summary: one-liner translatable description of the issue */
  gchar *summary;
  /* description: multi-line translatable description of:
  * * what the issue is (and why it's an issue)
  * * what the source problem could be
  * * pointers to fixing the issue
  */
  gchar *description;

  /* The name of the area of issue
   * this one is in */
  gchar *area;
  /*  The name of the issue type */
  gchar *name;

  /* default_level: The default level of severity for this
  * issue. */
  GstValidateReportLevel default_level;
};

I void gst_validate_report_init                            (void);
I void gst_validate_report_add_message                     (GstValidateReport *report,
                                                             const gchar *message);
I GstValidateReport *gst_validate_report_new               (GstValidateIssue * issue,
                                                            GstValidateReporter * reporter,
                                                            const gchar * message);
I GstValidateIssueId gst_validate_report_get_issue_id      (GstValidateReport * report);
I gboolean           gst_validate_report_check_abort       (GstValidateReport * report);
I void               gst_validate_report_printf            (GstValidateReport * report);
I void               gst_validate_report_print_level       (GstValidateReport *report);
I void               gst_validate_report_print_detected_on (GstValidateReport *report);
I void               gst_validate_report_print_details     (GstValidateReport *report);
I void               gst_validate_report_print_description (GstValidateReport *report);
I gboolean gst_validate_report_should_print                (GstValidateReport * report);
I gboolean gst_validate_report_set_master_report           (GstValidateReport *report,
                                                            GstValidateReport *master_report);
I void gst_validate_report_set_reporting_level             (GstValidateReport *report,
                                                            GstValidateReportingDetails level);
I void gst_validate_report_add_repeated_report             (GstValidateReport *report,
                                                            GstValidateReport *repeated_report);
I GstValidateReportLevel
gst_validate_report_level_from_name                        (const gchar *issue_name);

I void               gst_validate_issue_register           (GstValidateIssue * issue);
I GstValidateIssueId gst_validate_issue_get_id             (GstValidateIssue * issue);

/* Visible for the testsuite */
void               gst_validate_report_unref             (GstValidateReport * report);
GstValidateReport *gst_validate_report_ref               (GstValidateReport * report);
GstValidateIssue  *gst_validate_issue_from_id            (GstValidateIssueId issue_id);


#endif
