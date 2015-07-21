/* GStreamer
 *
 * Copyright (C) 2015 Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gst-validate-plugin.c
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "gst-validate-plugin.h"
#include "gst-validate-internal.h"

#define VALIDATE_PLUGIN_DATA "gst-validate-plugin-data"

typedef struct
{
  GstValidatePluginExitFunc exit;
  gpointer exit_udata;
} GstValidatePluginFuncs;


void
gst_validate_plugin_set_exit_function (GstPlugin * plugin,
    GstValidatePluginExitFunc exit_func, gpointer udata)
{
  GstValidatePluginFuncs *funcs = g_object_get_data (G_OBJECT (plugin),
      VALIDATE_PLUGIN_DATA);

  if (!funcs) {
    funcs = g_malloc0 (sizeof (GstValidatePluginFuncs));

    g_object_set_data_full (G_OBJECT (plugin), VALIDATE_PLUGIN_DATA,
        funcs, g_free);
  }

  funcs->exit = exit_func;
  funcs->exit_udata = udata;
}

void
gst_validate_plugins_exit_runner (GstValidateRunner * runner)
{
  GstRegistry *reg = gst_validate_registry_get ();
  GList *tmp, *plugins = gst_registry_get_plugin_list (reg);

  GST_DEBUG_OBJECT (runner, "===> Exiting plugins");
  for (tmp = plugins; tmp; tmp = tmp->next) {
    GstValidatePluginFuncs *funcs = g_object_get_data (tmp->data,
        VALIDATE_PLUGIN_DATA);

    if (funcs && funcs->exit)
      funcs->exit (tmp->data, runner, funcs->exit_udata);
  }

  gst_plugin_list_free (plugins);
}
