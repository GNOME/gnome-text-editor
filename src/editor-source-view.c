/* editor-source-view.c
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include "editor-source-view.h"

struct _EditorSourceView
{
  GtkSourceView parent_instance;
};

G_DEFINE_TYPE (EditorSourceView, editor_source_view, GTK_SOURCE_TYPE_VIEW)

static gboolean
on_key_pressed_cb (GtkEventControllerKey *key,
                   guint                  keyval,
                   guint                  keycode,
                   GdkModifierType        state,
                   GtkWidget             *widget)
{
  /* This seems to be the easiest way to reliably override the keybindings
   * from GtkTextView into something we want (which is to use them for moving
   * through the tabs.
   */

  if ((state & GDK_CONTROL_MASK) == 0)
    return FALSE;

  if (state & ~(GDK_CONTROL_MASK|GDK_SHIFT_MASK))
    return FALSE;

  switch (keyval)
    {
    case GDK_KEY_Page_Up:
    case GDK_KEY_KP_Page_Up:
      if (state & GDK_SHIFT_MASK)
        gtk_widget_activate_action (widget, "page.move-left", NULL);
      else
        gtk_widget_activate_action (widget, "win.focus-neighbor", "i", -1);
      return TRUE;

    case GDK_KEY_Page_Down:
    case GDK_KEY_KP_Page_Down:
      if (state & GDK_SHIFT_MASK)
        gtk_widget_activate_action (widget, "page.move-right", NULL);
      else
        gtk_widget_activate_action (widget, "win.focus-neighbor", "i", 1);
      return TRUE;

    default:
      break;
    }

  return FALSE;
}

static void
editor_source_view_class_init (EditorSourceViewClass *klass)
{
}

static void
editor_source_view_init (EditorSourceView *self)
{
  GtkEventController *controller;

  controller = gtk_event_controller_key_new ();
  g_signal_connect (controller,
                    "key-pressed",
                    G_CALLBACK (on_key_pressed_cb),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);
}
