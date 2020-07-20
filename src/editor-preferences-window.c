/* editor-preferences-window.c
 *
 * Copyright 2020 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "editor-preferences-window"

#include "config.h"

#include "editor-preferences-font.h"
#include "editor-preferences-radio.h"
#include "editor-preferences-row.h"
#include "editor-preferences-spin.h"
#include "editor-preferences-switch.h"
#include "editor-preferences-window.h"

struct _EditorPreferencesWindow
{
  GtkWindow parent_instance;
};

G_DEFINE_TYPE (EditorPreferencesWindow, editor_preferences_window, GTK_TYPE_WINDOW)

enum {
  CLOSE,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void
editor_preferences_window_row_activated_cb (EditorPreferencesWindow *self,
                                            EditorPreferencesRow    *row,
                                            GtkListBox              *list_box)
{
  g_assert (EDITOR_IS_PREFERENCES_WINDOW (self));
  g_assert (EDITOR_IS_PREFERENCES_ROW (row));
  g_assert (GTK_IS_LIST_BOX (list_box));

  editor_preferences_row_emit_activated (row);
}

static void
editor_preferences_window_close (EditorPreferencesWindow *self)
{
  g_assert (EDITOR_IS_PREFERENCES_WINDOW (self));

  gtk_window_close (GTK_WINDOW (self));
}

static void
editor_preferences_window_class_init (EditorPreferencesWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkBindingSet *bindings = gtk_binding_set_by_class (klass);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-preferences-window.ui");
  gtk_widget_class_bind_template_callback (widget_class, editor_preferences_window_row_activated_cb);

  signals [CLOSE] = g_signal_new_class_handler ("close",
                                                G_TYPE_FROM_CLASS (klass),
                                                G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
                                                G_CALLBACK (editor_preferences_window_close),
                                                NULL, NULL,
                                                NULL,
                                                G_TYPE_NONE, 0);

  gtk_binding_entry_add_signal (bindings, GDK_KEY_w, GDK_CONTROL_MASK, "close", 0);
  gtk_binding_entry_add_signal (bindings, GDK_KEY_Escape, 0, "close", 0);

  g_type_ensure (EDITOR_TYPE_PREFERENCES_FONT);
  g_type_ensure (EDITOR_TYPE_PREFERENCES_ROW);
  g_type_ensure (EDITOR_TYPE_PREFERENCES_RADIO);
  g_type_ensure (EDITOR_TYPE_PREFERENCES_SPIN);
  g_type_ensure (EDITOR_TYPE_PREFERENCES_SWITCH);
}

static void
editor_preferences_window_init (EditorPreferencesWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_window_set_default_size (GTK_WINDOW (self), 500, -1);
}

EditorPreferencesWindow *
editor_preferences_window_new (EditorApplication *application)
{
  return g_object_new (EDITOR_TYPE_PREFERENCES_WINDOW,
                       "application", application,
                       NULL);
}
