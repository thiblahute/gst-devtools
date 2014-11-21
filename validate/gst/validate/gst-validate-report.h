/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-monitor-report.h - Validate Element report structures and functions
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

#ifndef __GST_VALIDATE_REPORT_H__
#define __GST_VALIDATE_REPORT_H__

#include <glib-object.h>

typedef guintptr GstValidateIssueId;

#include <gst/gst.h>
#include <gst/validate/gst-validate-enums.h>
#include <gst/validate/gst-validate-reporter.h>

G_BEGIN_DECLS

GType           gst_validate_report_get_type (void);
#define GST_TYPE_VALIDATE_REPORT (gst_validate_report_get_type ())

#define GST_VALIDATE_ISSUE_FORMAT G_GUINTPTR_FORMAT " (%s) : %s: %s"
#define GST_VALIDATE_ISSUE_ARGS(i) gst_validate_issue_get_id (i), \
                                   gst_validate_report_level_get_name (i->default_level), \
                                   i->area, \
                                   i->summary

#define GST_VALIDATE_ERROR_REPORT_PRINT_FORMAT GST_TIME_FORMAT " <%s>: %" GST_VALIDATE_ISSUE_FORMAT ": %s"
#define GST_VALIDATE_REPORT_PRINT_ARGS(r) GST_TIME_ARGS (r->timestamp), \
                                    gst_validate_reporter_get_name (r->reporter), \
                                    GST_VALIDATE_ISSUE_ARGS (r->issue), \
                                    r->message

GstValidateIssue  *gst_validate_issue_new      (GstValidateIssueId issue_id,
                                                const gchar * summary,
                                                const gchar * description,
                                                GstValidateReportLevel default_level);
void gst_validate_issue_set_default_level      (GstValidateIssue *issue,
                                                GstValidateReportLevel default_level);


const gchar *      gst_validate_report_level_get_name (GstValidateReportLevel level);

void               gst_validate_printf        (gpointer source,
                                               const gchar      * format,
                                               ...) G_GNUC_PRINTF (2, 3) G_GNUC_NO_INSTRUMENT;
void               gst_validate_printf_valist (gpointer source,
                                               const gchar      * format,
                                               va_list            args) G_GNUC_NO_INSTRUMENT;

struct _GstValidateReport {
  gint    refcount;

  /* issue: The issue this report corresponds to (to get description, summary,...) */
  GstValidateIssue *issue;

  GstValidateReportLevel level;

  /* The reporter that reported the issue (to get names, info, ...) */
  GstValidateReporter *reporter;

  /* timestamp: The time at which this issue happened since
   * the process start (to stay in sync with gst logging) */
  GstClockTime timestamp;

  /* message: issue-specific message. Gives more detail on the actual
   * issue. Can be NULL */
  gchar *message;


  /* <private> */
  /* When reporter->intercept_report returns KEEP, the report is not
   * added to the runner. It can be added as a "shadow_report" to
   * the upstream report, which is tracked by the runner. */
  GMutex shadow_reports_lock;
  GstValidateReport *master_report;
  GList *shadow_reports;

  /* Lists the reports that were repeated inside the same reporter */
  GList *repeated_reports;

  GstValidateReportingDetails reporting_level;
};

G_END_DECLS

#endif /* __GST_VALIDATE_REPORT_H__ */

