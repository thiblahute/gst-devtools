/* GStreamer
 *
 * Copyright (C) 2015 Raspberry Pi Foundation
 *  Author: Thibault Saunier <thibault.saunier@collabora.com>
 *
 * gstvalidategtk.c: GstValidateActionTypes to use with gtk applications
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

#define _GNU_SOURCE

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gtk/gtk.h>
#include <gdk/gdk.h>

#include "../../gst/validate/gst-validate-report.h"
#include "../../gst/validate/gst-validate-reporter.h"
#include "../../gst/validate/validate.h"
#include "../../gst/validate/gst-validate-scenario.h"
#include "../../gst/validate/gst-validate-utils.h"

#define ACTION_GDKEVENTS_QUARK g_quark_from_static_string("ACTION_GDKEVENTS_QUARK")
static GList *awaited_actions = NULL;   /* A list of GstValidateAction to be executed */

static const gchar *
get_widget_name (GtkWidget * widget)
{
  const gchar *name = NULL;

  if (GTK_IS_BUILDABLE (widget))
    name = gtk_buildable_get_name (GTK_BUILDABLE (widget));

  if (!name) {
    AtkObject *accessible = gtk_widget_get_accessible (widget);

    if (accessible) {
      name = atk_object_get_name (accessible);
    }
  }

  if (!name)
    name = gtk_widget_get_name (widget);

  return name;
}

static GdkEventType
get_event_type (GstValidateScenario * scenario, GstValidateAction * action)
{
  guint type;
  const gchar *etype_str = gst_structure_get_string (action->structure, "type");

  if (!etype_str)
    return GDK_NOTHING;

  if (gst_validate_utils_enum_from_str (GDK_TYPE_EVENT_TYPE, etype_str, &type))
    return type;

  GST_VALIDATE_REPORT (scenario,
      g_quark_from_static_string ("scenario::execution-error"),
      "Uknown event type %s, the string should look like the ones defined in "
      "gdk_event_type_get_type", etype_str);

  return -2;
}

static GdkDevice *
get_device (GstValidateAction * action, GdkInputSource input_source)
{
  GList *tmp, *devices;
  GdkDevice *device = NULL;
  GdkDeviceManager *dev_manager;

  dev_manager = gdk_display_get_device_manager (gdk_display_get_default ());
  devices =
      gdk_device_manager_list_devices (dev_manager, GDK_DEVICE_TYPE_MASTER);

  for (tmp = devices; tmp; tmp = tmp->next) {
    if (gdk_device_get_source (tmp->data) == input_source) {
      device = tmp->data;
      break;
    }
  }

  g_list_free (devices);

  return device;
}

static GdkEvent *
_create_key_event (GdkWindow * window, GdkEventType etype, guint keyval,
    guint hw_keycode, guint state, GdkDevice * device)
{
  GdkEvent *event = gdk_event_new (etype);
  GdkEventKey *kevent = (GdkEventKey *) event;

  kevent->window = g_object_ref (window);
  kevent->send_event = TRUE;
  kevent->time = GDK_CURRENT_TIME;
  kevent->keyval = keyval;
  kevent->hardware_keycode = hw_keycode;
  kevent->state = state;

  gdk_event_set_device (event, device);

  return event;
}

static GList *
_create_keyboard_events (GstValidateAction * action,
    GdkWindow * window, const gchar * keyname, const gchar * string,
    GdkEventType etype)
{
  guint *keys;
  GList *events = NULL;
  GdkDevice *device = NULL;

  if (etype == GDK_NOTHING) {
    etype = GDK_KEY_PRESS;
  } else if (etype != GDK_KEY_PRESS && etype != GDK_KEY_RELEASE) {
    GST_VALIDATE_REPORT (action->scenario,
        g_quark_from_static_string ("scenario::execution-error"),
        "GdkEvent type %s does not work with the 'keys' parametter",
        gst_structure_get_string (action->structure, "type"));

    return NULL;
  }

  device = get_device (action, GDK_SOURCE_KEYBOARD);
  if (device == NULL) {
    GST_VALIDATE_REPORT (action->scenario,
        g_quark_from_static_string ("scenario::execution-error"),
        "Could not find a keyboard device");

    return NULL;
  }

  if (keyname) {
    guint keyval, state;

    gtk_accelerator_parse_with_keycode (keyname, &keyval, &keys, &state);
    events =
        g_list_append (events, _create_key_event (window, etype, keyval,
            keys ? keys[0] : 0, state, device));
  } else if (string) {
    gint i;

    for (i = 0; string[i]; i++) {
      gint n_keys;
      GdkKeymapKey *kmaps;
      guint keyval = gdk_unicode_to_keyval (string[i]);

      gdk_keymap_get_entries_for_keyval (gdk_keymap_get_default (),
          keyval, &kmaps, &n_keys);

      events =
          g_list_append (events, _create_key_event (window, etype, keyval,
              kmaps[0].keycode, 0, device));
    }
  }

  return events;
}

typedef struct
{
  gchar **widget_paths;
  const gchar *widget_label;
  GType widget_type;
  gint current_index;
  GtkWidget *widget;
  gboolean found;
} WidgetNameWidget;

static GtkWidget *_find_widget (GtkContainer * container,
    WidgetNameWidget * res);

static gboolean
_widget_matches (GtkWidget * widget, WidgetNameWidget * data)
{
  if (data->widget_paths && g_strcmp0 (get_widget_name (GTK_WIDGET (widget)),
          data->widget_paths[data->current_index]) == 0 &&
      (!data->widget_type || G_OBJECT_TYPE (widget) == data->widget_type)) {
    return TRUE;
  }

  if (G_OBJECT_TYPE (widget) == data->widget_type) {
    if (data->widget_label) {
      if (g_object_class_find_property (G_OBJECT_GET_CLASS (widget), "label")) {
        gchar *label;

        g_object_get (widget, "label", &label, NULL);
        if (g_strcmp0 (label, data->widget_label) == 0) {
          g_free (label);
          return TRUE;
        }

        g_free (label);
      }
    }
  }

  return FALSE;
}

static void
_find_widget_cb (GtkWidget * child, WidgetNameWidget * res)
{
  if (res->found) {
    return;
  }

  if (_widget_matches (child, res)) {
    res->current_index++;

    if (!res->widget_paths || res->widget_paths[res->current_index] == NULL) {
      res->widget = child;
      res->found = TRUE;
    } else if (GTK_CONTAINER (child)) {
      res->widget = _find_widget (GTK_CONTAINER (child), res);
    }

  } else {
    if (GTK_IS_CONTAINER (child)) {
      res->widget = _find_widget (GTK_CONTAINER (child), res);
    }
  }

}

static GtkWidget *
_find_widget (GtkContainer * container, WidgetNameWidget * res)
{
  if (res->found)
    return res->widget;

  if (_widget_matches (GTK_WIDGET (container), res)) {
    res->current_index++;

    if (!res->widget_paths || res->widget_paths[res->current_index] == NULL)
      return GTK_WIDGET (container);
  }

  gtk_container_forall (container, (GtkCallback) _find_widget_cb, res);

  if (res->widget) {
    res->current_index++;

    if (!res->widget_paths || res->widget_paths[res->current_index + 1] == NULL)
      return res->widget;

    if (GTK_IS_CONTAINER (res->widget))
      _find_widget (GTK_CONTAINER (res->widget), res);
  }

  return res->widget;
}


static void
_find_button (GtkWidget * widget, GtkWidget ** button)
{
  if (GTK_IS_BUTTON (widget))
    *button = widget;
}

/*  Copy pasted from gtk+/gtk/gtktestutils.c */
static GSList *
test_find_widget_input_windows (GtkWidget * widget, gboolean input_only)
{
  GdkWindow *window, *parent_window;
  GList *node, *children;
  GSList *matches = NULL;
  gpointer udata;

  window = gtk_widget_get_window (widget);

  gdk_window_get_user_data (window, &udata);
  if (udata == widget && (!input_only || (GDK_IS_WINDOW (window)
              && gdk_window_is_input_only (GDK_WINDOW (window)))))
    matches = g_slist_prepend (matches, window);

  parent_window = gtk_widget_get_parent_window (widget);
  if (!parent_window)
    return g_slist_reverse (matches);

  children = gdk_window_get_children (parent_window);
  for (node = children; node; node = node->next) {
    gdk_window_get_user_data (node->data, &udata);
    if (udata == widget && (!input_only || (GDK_IS_WINDOW (node->data)
                && gdk_window_is_input_only (GDK_WINDOW (node->data)))))
      matches = g_slist_prepend (matches, node->data);
  }
  return g_slist_reverse (matches);
}

static GdkWindow *
widget_get_window (GtkWidget * widget)
{
  GdkWindow *res = NULL;
  GSList *iwindows = test_find_widget_input_windows (widget, FALSE);

  if (!iwindows)
    iwindows = test_find_widget_input_windows (widget, TRUE);

  if (iwindows)
    res = iwindows->data;
  else if (GTK_IS_CONTAINER (widget)) {
    GList *tmp, *children;;

    children = gtk_container_get_children (GTK_CONTAINER (widget));
    for (tmp = children; tmp; tmp = tmp->next) {
      res = widget_get_window (tmp->data);
      if (res) {
        g_list_free (children);
        return res;
      }
    }
    g_list_free (children);
  }

  g_slist_free (iwindows);

  return res;
}

static GdkWindow *
get_window (GstValidateScenario * scenario, GstValidateAction * action,
    const gchar * widget_name)
{
  GList *tmptoplevel;
  GdkWindow *res = NULL;
  gchar **widget_paths = NULL;
  const gchar *widget_type = NULL, *widget_label;

  GList *toplevels = gtk_window_list_toplevels ();

  if (!widget_name)
    widget_name = gst_structure_get_string (action->structure, "widget-name");
  widget_type = gst_structure_get_string (action->structure, "widget-type");
  widget_label = gst_structure_get_string (action->structure, "widget-label");

  if (!toplevels) {
    GST_VALIDATE_REPORT (scenario,
        g_quark_from_static_string ("scenario::execution-error"),
        "No Gtk topelevel window found, can not sent GdkEvent");

    return NULL;
  }

  if (!widget_name && !widget_type) {
    res = gtk_widget_get_window (toplevels->data);

    goto done;
  }

  if (widget_name)
    widget_paths = g_strsplit (widget_name, "/", -1);

  for (tmptoplevel = toplevels; tmptoplevel; tmptoplevel = tmptoplevel->next) {
    GtkWidget *widget;
    WidgetNameWidget wn = { 0, };

    wn.widget_paths = widget_paths;
    if (widget_type)
      wn.widget_type = g_type_from_name (widget_type);
    wn.widget_label = widget_label;
    wn.current_index = 0;
    wn.found = FALSE;
    wn.widget = NULL;

    widget = _find_widget (tmptoplevel->data, &wn);
    if (widget) {
      /* Copied from gdk testing utils */
      if (GTK_IS_TOOL_BUTTON (widget)) {
        GST_INFO_OBJECT (scenario, "Widget %s is tool button,"
            " getting the actual button", widget_name);
        gtk_container_forall (GTK_CONTAINER (widget),
            (GtkCallback) _find_button, &widget);
      }

      res = widget_get_window (widget);
      break;
    }
  }

done:
  g_list_free (toplevels);

  return res;
}

static GstValidateActionReturn
_put_events (GstValidateAction * action, GList * events)
{
  GList *tmp;
  gboolean block = FALSE;
  GMainContext *ctx;

  if (events == NULL)
    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;

  gst_mini_object_set_qdata (GST_MINI_OBJECT (action), ACTION_GDKEVENTS_QUARK,
      events, NULL);
  awaited_actions = g_list_append (awaited_actions, action);
  gst_structure_get_boolean (action->structure, "block", &block);

  for (tmp = events; tmp; tmp = tmp->next) {
    gdk_event_put (tmp->data);
  }

  if (!block)
    return GST_VALIDATE_EXECUTE_ACTION_ASYNC;

  ctx = g_main_context_default ();
  while (events) {
    g_main_context_iteration (ctx, TRUE);

    events = ((GList *) gst_mini_object_get_qdata (GST_MINI_OBJECT (action),
            ACTION_GDKEVENTS_QUARK));
  }

  return GST_VALIDATE_EXECUTE_ACTION_OK;
}

static GstValidateActionReturn
_execute_key_events (GstValidateScenario * scenario, GstValidateAction * action,
    GdkEventType etype, GdkWindow * window)
{
  GList *events = NULL;
  const gchar *keys, *string;

  keys = gst_structure_get_string (action->structure, "keys");
  string = gst_structure_get_string (action->structure, "string");
  if (keys || string) {
    events = _create_keyboard_events (action, window, keys, string, etype);

    return _put_events (action, events);
  }

  return -1;
}

static GdkEvent *
_create_button_event (GdkWindow * window, GdkEventType etype, gint modifiers,
    gint button, GdkDevice * device, gdouble x, gdouble y)
{
  GdkEvent *event = gdk_event_new (etype);
  GdkEventButton *bevent = (GdkEventButton *) event;

  bevent->window = g_object_ref (window);
  bevent->send_event = TRUE;
  bevent->time = GDK_CURRENT_TIME;
  bevent->button = button;
  bevent->state = modifiers;
  bevent->x = x;
  bevent->y = y;

  gdk_event_set_device (event, device);

  return event;
}

static GList *
_create_button_events (GstValidateAction * action,
    GdkWindow * window, GdkEventType etype, gint button,
    GdkModifierType modifiers, gdouble x, gdouble y)
{
  GList *res = NULL;
  GdkDevice *device = NULL;

  device = get_device (action, GDK_SOURCE_KEYBOARD);
  if (device == NULL) {
    GST_VALIDATE_REPORT (action->scenario,
        g_quark_from_static_string ("scenario::execution-error"),
        "Could not find a keyboard device");

    return NULL;
  }

  if (etype == GDK_NOTHING) {
    res = g_list_append (res,
        _create_button_event (window, GDK_BUTTON_PRESS,
            modifiers, button, device, x, y));

    res = g_list_append (res,
        _create_button_event (window, GDK_BUTTON_RELEASE,
            modifiers, button, device, x, y));
  }

  return res;
}

static GstValidateActionReturn
_execute_button_events (GstValidateScenario * scenario,
    GstValidateAction * action, GdkEventType etype, GdkWindow * window)
{
  gint button = 1;
  gboolean handled = FALSE;
  GList *events = NULL;
  gint modifiers = 0;
  gdouble x = 0, y = 0;

  if (gst_structure_get_int (action->structure, "button", &button))
    handled = TRUE;

  if (!gst_structure_get_int (action->structure, "modifiers", &modifiers)) {
    const gchar *modifiers_str = gst_structure_get_string (action->structure,
        "modifiers");

    if (modifiers_str) {
      modifiers = gdk_keyval_from_name (modifiers_str);

      if (modifiers == GDK_KEY_VoidSymbol) {
        GST_ERROR_OBJECT (scenario, "Modifier: %s no valid!", modifiers_str);
      }
    }
  }

  if (handled || (etype == GDK_BUTTON_PRESS ||
          etype == GDK_2BUTTON_PRESS ||
          etype == GDK_BUTTON_PRESS ||
          etype == GDK_DOUBLE_BUTTON_PRESS ||
          etype == GDK_3BUTTON_PRESS || etype == GDK_BUTTON_RELEASE)) {
    gst_structure_get_double (action->structure, "x", &x);
    gst_structure_get_double (action->structure, "y", &y);

    events =
        _create_button_events (action, window, etype, button, modifiers, x, y);

    return _put_events (action, events);
  }

  return -1;
}

static gboolean
_execute_put_events (GstValidateScenario * scenario, GstValidateAction * action)
{
  GdkEventType etype;
  gint res;

  GdkWindow *window = get_window (scenario, action, NULL);

  if (!window)
    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;

  etype = get_event_type (scenario, action);
  if (etype == -2)
    return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;

  if ((res = _execute_key_events (scenario, action, etype, window)) > 0)
    return res;

  if ((res = _execute_button_events (scenario, action, etype, window)) > 0)
    return res;

  GST_VALIDATE_REPORT (scenario,
      g_quark_from_static_string ("scenario::execution-error"),
      "Action parametters not supported yet");

  return GST_VALIDATE_EXECUTE_ACTION_ERROR_REPORTED;
}

static void
_process_event (GdkEvent * event, gpointer data)
{
  GList *tmp;
  GdkEvent *done_event = NULL;
  GstValidateAction *action = NULL;

  for (tmp = awaited_actions; tmp; tmp = tmp->next) {
    GstValidateAction *tmp_action = tmp->data;
    GdkEvent *awaited_event =
        ((GList *) gst_mini_object_get_qdata (GST_MINI_OBJECT (tmp_action),
            ACTION_GDKEVENTS_QUARK))->data;

    if (awaited_event->type == event->type
        && ((GdkEventAny *) event)->window ==
        ((GdkEventAny *) awaited_event)->window) {

      switch (awaited_event->type) {
        case GDK_KEY_PRESS:
        case GDK_KEY_RELEASE:
          if (event->key.keyval == awaited_event->key.keyval) {
            done_event = awaited_event;
            action = tmp_action;
          }
          break;
        case GDK_BUTTON_PRESS:
        case GDK_DOUBLE_BUTTON_PRESS:
        case GDK_3BUTTON_PRESS:
        case GDK_BUTTON_RELEASE:
          if (event->button.button == awaited_event->button.button &&
              event->button.state == awaited_event->button.state) {
            done_event = awaited_event;
            action = tmp_action;
          }
          break;
        default:
          GST_ERROR ("Type %d not handled", awaited_event->type);
          g_assert_not_reached ();
      }
    }
  }

  if (done_event) {
    GList *awaited_events = gst_mini_object_get_qdata (GST_MINI_OBJECT (action),
        ACTION_GDKEVENTS_QUARK);

    awaited_events = g_list_remove (awaited_events, done_event);
    gdk_event_free (done_event);
    gst_mini_object_set_qdata (GST_MINI_OBJECT (action), ACTION_GDKEVENTS_QUARK,
        awaited_events, NULL);

    if (awaited_events == NULL) {
      awaited_actions = g_list_remove (awaited_actions, action);
      gst_validate_action_set_done (action);
    }
  }

  gtk_main_do_event (event);
}

static gboolean
gst_validate_gtk_init (GstPlugin * plugin)
{
  gdk_event_handler_set (_process_event, NULL, NULL);

/*  *INDENT-OFF* */
  gst_validate_register_action_type_dynamic (plugin, "gtk-put-event",
      GST_RANK_PRIMARY, _execute_put_events, ((GstValidateActionParameter[]) {
            {
              .name = "keys",
              .description = "The keyboard keys to be used for the event, parsed"
              " with gtk_accelerator_parse_with_keycode, so refer to its documentation"
              " for more information",
              .mandatory = FALSE,
              .types = "string",
              .possible_variables = NULL,
            },
            {
              .name = "string",
              .description = "The string to be 'written' by the keyboard"
              " sending KEY_PRESS GdkEvents",
              .mandatory = FALSE,
              .types = "string",
              .possible_variables = NULL,
            },
            {
              .name = "type",
              .description = "The event type to get executed. "
              "the string should look like the ones in GdkEventType but without"
              " the leading 'GDK_'. It is not mandatory as it can be computed from"
              " other present fields (e.g, an action with 'keys' will concider the type"
              " as 'key_pressed' by default).",
              .mandatory = FALSE,
              .types = "string",
            },
            {
              .name = "widget-name",
              .description = "The name of the target GdkWidget of the GdkEvent"
                ". That widget has to contain a GdkWindow. If not specified,"
                " the event will be sent to the first toplevel window",
              .mandatory = FALSE,
              .types = "string",
              .possible_variables = NULL,
            },
            {
              .name = "widget-type",
              .description = "The GType of the widget as a string",
              .mandatory = FALSE,
              .types = "string",
              .possible_variables = NULL,
            },
            {
              .name = "widget-label",
              .description = "The 'label' property of a widget",
              .mandatory = FALSE,
              .types = "string",
              .possible_variables = NULL,
            },
            {
              .name = "button",
              .description = "The button to simlulate a click on "
                  "(as defined by GDK_BUTTON_[PRIMARY, MIDDLE, SECONDARY])."
                  " Note that if only providing a button, without specifying"
                  " if press or release as a event type, it will generate a"
                  " 'click' (button-press + button-release event).",
              .mandatory = FALSE,
              .types = "int",
              .possible_variables = NULL,
              .def = "1",
            },
            {
              .name = "block",
              .description = "Blocks while the event is being processed by Gtk",
              .mandatory = FALSE,
              .types = "boolean",
              .possible_variables = NULL,
              .def = "false",
            },
            {NULL}
          }),
      "Put a GdkEvent on the event list using gdk_put_event",
      GST_VALIDATE_ACTION_TYPE_NO_EXECUTION_NOT_FATAL |
      GST_VALIDATE_ACTION_TYPE_DOESNT_NEED_PIPELINE);
/*  *INDENT-ON* */

  return TRUE;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    gstvalidategtk,
    "GstValidate plugin to execute action specific to the Gtk toolkit",
    gst_validate_gtk_init, VERSION, "LGPL", GST_PACKAGE_NAME,
    GST_PACKAGE_ORIGIN)
