/* GStreamer
 *
 * Copyright (C) 2014 Collabora Ltd.
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
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

#ifndef GST_VALIDATE_TRANSCODING_H
#define GST_VALIDATE_TRANSCODING_H 
#include <gst/gst.h>
#include <gst/validate/validate.h>

#include <gst/gst.h>
#include <gst/validate/validate.h>

GstElement *
gst_validate_transcoding_build_pipeline (int argc, gchar ** argv,
  GstValidateRunner **runner,
  GstValidateMonitor **monitor, gchar **error,
  GOptionEntry * extra_options);

#endif
