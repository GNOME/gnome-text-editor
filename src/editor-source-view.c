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

#include "editor-document-private.h"
#include "editor-source-view.h"
#include "editor-joined-menu-private.h"
#include "editor-spell-menu.h"

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
tweak_gutter_spacing (GtkSourceView *view)
{
  GtkSourceGutter *gutter;
  GtkWidget *child;
  guint n = 0;

  g_assert (GTK_SOURCE_IS_VIEW (view));

  /* Ensure we have a line gutter renderer to tweak */
  gutter = gtk_source_view_get_gutter (view, GTK_TEXT_WINDOW_LEFT);
  gtk_source_view_set_show_line_numbers (view, TRUE);

  /* Add margin to first gutter renderer */
  for (child = gtk_widget_get_first_child (GTK_WIDGET (gutter));
       child != NULL;
       child = gtk_widget_get_next_sibling (child), n++)
    {
      if (GTK_SOURCE_IS_GUTTER_RENDERER (child))
        gtk_widget_set_margin_start (child, n == 0 ? 4 : 0);
    }
}

static void
on_notify_buffer_cb (EditorSourceView *self,
                     GParamSpec       *pspec,
                     gpointer          unused)
{
  GtkTextBuffer *buffer;

  g_assert (EDITOR_IS_SOURCE_VIEW (self));

  buffer = gtk_text_view_get_buffer (GTK_TEXT_VIEW (self));

  if (EDITOR_IS_DOCUMENT (buffer))
    _editor_document_attach_actions (EDITOR_DOCUMENT (buffer), GTK_WIDGET (self));
}

static void
editor_source_view_class_init (EditorSourceViewClass *klass)
{
}

static void
editor_source_view_init (EditorSourceView *self)
{
  g_autoptr(GMenuModel) spelling_menu = NULL;
  g_autoptr(EditorJoinedMenu) joined = NULL;
  GtkEventController *controller;
  GMenuModel *extra_menu;

  g_signal_connect (self,
                    "notify::buffer",
                    G_CALLBACK (on_notify_buffer_cb),
                    NULL);

  controller = gtk_event_controller_key_new ();
  g_signal_connect (controller,
                    "key-pressed",
                    G_CALLBACK (on_key_pressed_cb),
                    self);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);

  tweak_gutter_spacing (GTK_SOURCE_VIEW (self));

  joined = editor_joined_menu_new ();
  extra_menu = gtk_text_view_get_extra_menu (GTK_TEXT_VIEW (self));
  editor_joined_menu_append_menu (joined, extra_menu);

  spelling_menu = editor_spell_menu_new ();
  editor_joined_menu_append_menu (joined, spelling_menu);

  gtk_text_view_set_extra_menu (GTK_TEXT_VIEW (self), G_MENU_MODEL (joined));
}
