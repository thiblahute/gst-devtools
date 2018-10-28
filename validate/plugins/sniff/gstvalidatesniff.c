#include <gst/gst.h>
#include "../../gst/validate/validate.h"
#include "../../gst/validate/gst-validate-utils.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#define VALIDATE_SNIFF_MISMATCH g_quark_from_static_string ("validatesniff::mismatch")

typedef struct _ValidateSniffOverride
{
  GstValidateOverride parent;

  const gchar* pad_name;
  gboolean record_buffers;
  GList* record_event_types;
  FILE* output_file;

} ValidateSniffOverride;

#define VALIDATE_TYPE_SNIFF_OVERRIDE validate_sniff_override_get_type ()
G_DECLARE_FINAL_TYPE (ValidateSniffOverride, validate_sniff_override, VALIDATE, SNIFF_OVERRIDE, GstValidateOverride)

G_DEFINE_TYPE (ValidateSniffOverride, validate_sniff_override, GST_TYPE_VALIDATE_OVERRIDE)

void
validate_sniff_override_init (ValidateSniffOverride* self)
{
}

void
validate_sniff_override_class_init (ValidateSniffOverrideClass* self)
{
}

static void
validate_sniff_override_buffer_handler (GstValidateOverride *override,
                                        GstValidateMonitor *pad_monitor,
                                        GstBuffer *buffer)
{
  ValidateSniffOverride* sniff = VALIDATE_SNIFF_OVERRIDE (override);
  char* line;

  abort();
  line = gst_info_strdup_printf("%" GST_PTR_FORMAT "\n", buffer);
  if (fputs(line, sniff->output_file) == EOF) {
    GST_ERROR_OBJECT(sniff, "Writing to file failed.");
    return;
  }
  fflush(sniff->output_file);
  free(line);
}

static ValidateSniffOverride*
validate_sniff_override_new (GstStructure* config)
{
  ValidateSniffOverride *sniff;
  GstValidateOverride *override;
  const gchar* output_dir;
  gchar* output_file_path = NULL;

  sniff = g_object_new (VALIDATE_TYPE_SNIFF_OVERRIDE, NULL);
  override = GST_VALIDATE_OVERRIDE (sniff);

  sniff->pad_name = gst_structure_get_string(config, "pad");
  if (!sniff->pad_name) {
    GST_ERROR_OBJECT(sniff, "pad property is mandatory");
    goto fail;
  }

  sniff->record_buffers = FALSE;
  gst_structure_get_boolean(config, "record-buffers", &sniff->record_buffers);

  output_dir = gst_structure_get_string(config, "output-dir");
  if (!output_dir) {
    GST_ERROR_OBJECT(sniff, "output-dir property is mandatory");
    goto fail;
  }

  if (g_mkdir_with_parents(output_dir, 0755) < 0) {
    GST_ERROR_OBJECT(sniff, "Failed to create directory: %s", output_dir);
    goto fail;
  }

  output_file_path = g_build_filename(output_dir, sniff->pad_name, NULL);
  sniff->output_file = fopen(output_file_path, "w");
  if (!sniff->output_file) {
    GST_ERROR_OBJECT(sniff, "Could not open for writing: %s", output_file_path);
    free(output_file_path);
    output_file_path = NULL;
    goto fail;
  }
  free(output_file_path);
  output_file_path = NULL;

  gst_validate_override_register_by_name (sniff->pad_name, override);

  override->buffer_handler = validate_sniff_override_buffer_handler;

  return sniff;

fail:
  g_object_unref(sniff);
  return NULL;
}

static gboolean
gst_validate_sniff_init (GstPlugin * plugin)
{
  GList *tmp;
  GList *config_list = gst_validate_plugin_get_config (plugin);

  if (!config_list)
    return TRUE;

  for (tmp = config_list; tmp; tmp = tmp->next) {
    GstStructure *config = tmp->data;
    validate_sniff_override_new (config);
  }

  gst_validate_issue_register (gst_validate_issue_new
      (VALIDATE_SNIFF_MISMATCH,
          "The recorded log does not match the expectation file.",
          "The recorded log does not match the expectation file.",
          GST_VALIDATE_REPORT_LEVEL_CRITICAL));

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    validatesniff,
    "GstValidate plugin that records buffers and events on specified pads and matches the log with expectation files.",
    gst_validate_sniff_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
