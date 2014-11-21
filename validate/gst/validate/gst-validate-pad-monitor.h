/* GStreamer
 * Copyright (C) 2013 Thiago Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-pad-monitor.h - Validate PadMonitor class
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

#ifndef __GST_VALIDATE_PAD_MONITOR_H__
#define __GST_VALIDATE_PAD_MONITOR_H__

#include <glib-object.h>
#include <gst/gst.h>

typedef struct _GstValidatePadMonitor GstValidatePadMonitor;
typedef struct _GstValidatePadMonitorClass GstValidatePadMonitorClass;

#include <gst/validate/gst-validate-monitor.h>
#include <gst/validate/media-descriptor-parser.h>
#include <gst/validate/gst-validate-element-monitor.h>

G_BEGIN_DECLS

#define GST_TYPE_VALIDATE_PAD_MONITOR			(gst_validate_pad_monitor_get_type ())
#define GST_IS_VALIDATE_PAD_MONITOR(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_VALIDATE_PAD_MONITOR))
#define GST_IS_VALIDATE_PAD_MONITOR_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_VALIDATE_PAD_MONITOR))
#define GST_VALIDATE_PAD_MONITOR_GET_CLASS(obj)	(G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_VALIDATE_PAD_MONITOR, GstValidatePadMonitorClass))
#define GST_VALIDATE_PAD_MONITOR(obj)			(G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_VALIDATE_PAD_MONITOR, GstValidatePadMonitor))
#define GST_VALIDATE_PAD_MONITOR_CLASS(klass)		(G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_VALIDATE_PAD_MONITOR, GstValidatePadMonitorClass))
#define GST_VALIDATE_PAD_MONITOR_CAST(obj)            ((GstValidatePadMonitor*)(obj))
#define GST_VALIDATE_PAD_MONITOR_CLASS_CAST(klass)    ((GstValidatePadMonitorClass*)(klass))

#define GST_VALIDATE_PAD_MONITOR_GET_PAD(m) (GST_PAD_CAST (GST_VALIDATE_MONITOR_GET_OBJECT (m)))


/* normal GObject stuff */
GType		gst_validate_pad_monitor_get_type		(void);

GstValidatePadMonitor *   gst_validate_pad_monitor_new      (GstPad * pad, GstValidateRunner * runner, GstValidateElementMonitor *element_monitor);

G_END_DECLS

#endif /* __GST_VALIDATE_PAD_MONITOR_H__ */

