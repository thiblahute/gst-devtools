/* GStreamer
 *
 * Copyright (C) 2014 Thibault Saunier <tsaunier@gnome.org>
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

#ifndef _GST_VALIDATE__TYPES_H
#define _GST_VALIDATE__TYPES_H 

typedef struct _GstValidateScenario             GstValidateScenario;
typedef struct _GstValidateScenarioClass        GstValidateScenarioClass;

typedef struct _GstValidateMonitor              GstValidateMonitor;
typedef struct _GstValidateMonitorClass         GstValidateMonitorClass;

typedef struct _GstValidateRunner               GstValidateRunner;
typedef struct _GstValidateRunnerClass          GstValidateRunnerClass;

typedef struct _GstValidateElementMonitor       GstValidateElementMonitor;
typedef struct _GstValidateElementMonitorClass  GstValidateElementMonitorClass;

typedef struct _GstValidateBinMonitor           GstValidateBinMonitor;
typedef struct _GstValidateBinMonitorClass      GstValidateBinMonitorClass;

typedef struct _GstValidatePipelineMonitor      GstValidatePipelineMonitor;
typedef struct _GstValidatePipelineMonitorClass GstValidatePipelineMonitorClass;

typedef struct _GstValidateIssue GstValidateIssue;

/**
 * GstValidateReport:
 *
 * An opaque structure representing the reporting of a specific GstValidate issue
 */
typedef struct _GstValidateReport GstValidateReport;

#endif
