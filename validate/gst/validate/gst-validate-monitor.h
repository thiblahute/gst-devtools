/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-monitor.h - Validate Monitor abstract base class
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

#ifndef __GST_VALIDATE_MONITOR_H__
#define __GST_VALIDATE_MONITOR_H__

#include <glib-object.h>
#include <gst/gst.h>

#include "gst-validate-types.h"
#include <gst/validate/gst-validate-report.h>
#include <gst/validate/gst-validate-runner.h>
#include "media-descriptor-parser.h"

G_BEGIN_DECLS

#define GST_TYPE_VALIDATE_MONITOR			          (gst_validate_monitor_get_type ())
#define GST_IS_VALIDATE_MONITOR(obj)		        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VALIDATE_MONITOR))
#define GST_IS_VALIDATE_MONITOR_CLASS(klass)	  (G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VALIDATE_MONITOR))
#define GST_VALIDATE_MONITOR_GET_CLASS(obj)	    (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VALIDATE_MONITOR, GstValidateMonitorClass))
#define GST_VALIDATE_MONITOR(obj)			          (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VALIDATE_MONITOR, GstValidateMonitor))
#define GST_VALIDATE_MONITOR_CLASS(klass)		    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VALIDATE_MONITOR, GstValidateMonitorClass))
#define GST_VALIDATE_MONITOR_CAST(obj)          ((GstValidateMonitor*)(obj))
#define GST_VALIDATE_MONITOR_CLASS_CAST(klass)  ((GstValidateMonitorClass*)(klass))

/* normal GObject stuff */
GType		gst_validate_monitor_get_type		(void);

void gst_validate_monitor_set_media_descriptor (GstValidateMonitor * monitor,
                                                GstMediaDescriptor *media_descriptor);
G_END_DECLS

#endif /* __GST_VALIDATE_MONITOR_H__ */

