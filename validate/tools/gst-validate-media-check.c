/* GStreamer
 *
 * Copyright (C) 2013 Collabora Ltd.
 *  Author: Thiago Sousa Santos <thiago.sousa.santos@collabora.com>
 *
 * gst-validate-media-check.c - Media Check CLI tool
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

#include "../gst/validate/media-descriptor-writer.h"
#include "../gst/validate/media-descriptor-parser.h"
#include "../gst/validate/media-descriptor.h"

int
main (int argc, gchar ** argv)
{
  GOptionContext *ctx;

  guint ret = 0;
  GError *err = NULL;
  gboolean full = FALSE;
  gchar *output_file = NULL;
  gchar *expected_file = NULL;
  gchar *output = NULL;
  GstMediaDescriptorWriter *writer;
  GstValidateRunner *runner;
  GstMediaDescriptorParser *reference = NULL;

  GOptionEntry options[] = {
    {"output-file", 'o', 0, G_OPTION_ARG_FILENAME,
          &output_file, "The output file to store the results",
        NULL},
    {"full", 'f', 0, G_OPTION_ARG_NONE,
          &full, "Fully analize the file frame by frame",
        NULL},
    {"expected-results", 'e', 0, G_OPTION_ARG_FILENAME,
          &expected_file, "Path to file containing the expected results "
          "(or the last results found) for comparison with new results",
        NULL},
    {NULL}
  };

  g_set_prgname ("gst-validate-media-check-" GST_API_VERSION);
  ctx = g_option_context_new ("[URI]");
  g_option_context_set_summary (ctx, "Analizes a media file and writes "
      "the results to stdout or a file. Can also compare the results found "
      "with another results file for identifying regressions. The monitoring"
      " lib from gst-validate will be enabled during the tests to identify "
      "issues with the gstreamer elements involved with the media file's "
      "container and codec types");
  g_option_context_add_main_entries (ctx, options, NULL);

  if (!g_option_context_parse (ctx, &argc, &argv, &err)) {
    g_printerr ("Error initializing: %s\n", err->message);
    g_option_context_free (ctx);
    exit (1);
  }

  gst_init (&argc, &argv);
  gst_validate_init ();

  if (argc != 2) {
    gchar *msg = g_option_context_get_help (ctx, TRUE, NULL);
    g_printerr ("%s\n", msg);
    g_free (msg);
    g_option_context_free (ctx);
    return 1;
  }
  g_option_context_free (ctx);

  runner = gst_validate_runner_new ();
  writer =
      gst_media_descriptor_writer_new_discover (runner, argv[1], full, &err);
  if (writer == NULL) {
    g_print ("Could not discover file: %s", argv[1]);
    return 1;
  }

  if (output_file)
    gst_media_descriptor_writer_write (writer, output_file);

  if (expected_file) {
    reference = gst_media_descriptor_parser_new (runner, expected_file, &err);

    if (reference == NULL) {
      g_print ("Could not parse file: %s", expected_file);
      gst_object_unref (writer);

      return 1;
    }

    gst_media_descriptors_compare (GST_MEDIA_DESCRIPTOR (reference),
        GST_MEDIA_DESCRIPTOR (writer));
  } else {
    output = gst_media_descriptor_writer_serialize (writer);
    g_print ("Media info:\n%s\n", output);
    g_free (output);
  }

  ret = gst_validate_runner_printf (runner);
  if (ret && expected_file) {
    output = gst_media_descriptor_writer_serialize (writer);
    g_print ("Media info:\n%s\n", output);
    g_free (output);
  }

  if (reference)
    gst_object_unref (reference);
  gst_object_unref (writer);
  gst_object_unref (runner);

  return ret;
}
