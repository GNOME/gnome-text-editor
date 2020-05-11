/* editor-sidebar.c
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

#define G_LOG_DOMAIN "editor-sidebar"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-application.h"
#include "editor-document.h"
#include "editor-page.h"
#include "editor-session.h"
#include "editor-sidebar-item-private.h"
#include "editor-sidebar-model-private.h"
#include "editor-sidebar-private.h"
#include "editor-sidebar-row-private.h"
#include "editor-window.h"

struct _EditorSidebar
{
  GtkWidget           parent_instance;

  EditorSidebarModel *model;

  GtkEntry           *search_entry;
  GtkListBox         *list_box;
  GtkRevealer        *revealer;
  GtkButton          *open_button;
};

G_DEFINE_TYPE (EditorSidebar, editor_sidebar, GTK_TYPE_WIDGET)

static void
editor_sidebar_notify_child_revealed_cb (EditorSidebar *self,
                                         GParamSpec    *pspec,
                                         GtkRevealer   *revealer)
{
  g_assert (EDITOR_IS_SIDEBAR (self));
  g_assert (GTK_IS_REVEALER (revealer));

  if (!gtk_revealer_get_child_revealed (revealer))
    gtk_widget_hide (GTK_WIDGET (self));
}

/**
 * editor_sidebar_new:
 *
 * Create a new #EditorSidebar.
 *
 * Returns: (transfer full): a newly created #EditorSidebar
 */
EditorSidebar *
editor_sidebar_new (void)
{
  return g_object_new (EDITOR_TYPE_SIDEBAR, NULL);
}

static GtkWidget *
editor_sidebar_create_row_cb (gpointer item,
                              gpointer user_data)
{
  g_assert (EDITOR_IS_SIDEBAR_ITEM (item));
  g_assert (EDITOR_IS_SIDEBAR (user_data));

  return _editor_sidebar_row_new (item);
}

static void
editor_sidebar_row_activated_cb (EditorSidebar    *self,
                                 EditorSidebarRow *row,
                                 GtkListBox       *list_box)
{
  EditorSidebarItem *item;
  EditorSession *session;
  GtkWidget *window;

  g_assert (EDITOR_IS_SIDEBAR (self));
  g_assert (EDITOR_IS_SIDEBAR_ROW (row));
  g_assert (GTK_IS_LIST_BOX (list_box));

  window = gtk_widget_get_ancestor (GTK_WIDGET (self), EDITOR_TYPE_WINDOW);
  session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);
  item = _editor_sidebar_row_get_item (row);

  _editor_sidebar_item_open (item, session, EDITOR_WINDOW (window));

  gtk_widget_hide (GTK_WIDGET (self));
}

static gboolean
editor_sidebar_filter_func_cb (gpointer itemptr,
                               gpointer user_data)
{
  EditorSidebarItem *item = itemptr;
  GPatternSpec *spec = user_data;

  g_assert (EDITOR_IS_SIDEBAR_ITEM (item));
  g_assert (spec != NULL);

  return _editor_sidebar_item_matches (item, spec);
}

static void
editor_sidebar_search_entry_activate_cb (EditorSidebar *self,
                                         GtkEntry      *search_entry)
{
  GtkListBoxRow *row;

  g_assert (EDITOR_IS_SIDEBAR (self));
  g_assert (GTK_IS_ENTRY (search_entry));

  /* Activate the first row in the list box (if any) */
  if ((row = gtk_list_box_get_row_at_index (self->list_box, 0)))
    {
      gtk_editable_set_text (GTK_EDITABLE (search_entry), "");
      gtk_widget_activate (GTK_WIDGET (row));
    }
}

static void
editor_sidebar_search_entry_changed_cb (EditorSidebar *self,
                                        GtkEntry      *search_entry)
{
  g_autoptr(GtkFilterListModel) filter = NULL;
  const gchar *text;
  GListModel *model;

  g_assert (EDITOR_IS_SIDEBAR (self));
  g_assert (GTK_IS_ENTRY (search_entry));

  text = gtk_editable_get_text (GTK_EDITABLE (search_entry));

  if (text == NULL || text[0] == 0)
    {
      model = G_LIST_MODEL (self->model);
    }
  else
    {
      g_autofree gchar *text_fold = g_utf8_casefold (text, -1);
      g_autofree gchar *pattern = g_strdup_printf ("*%s*", g_strdelimit (text_fold, " \n\t", '*'));

      filter = gtk_filter_list_model_new (G_LIST_MODEL (self->model),
                                          editor_sidebar_filter_func_cb,
                                          g_pattern_spec_new (pattern),
                                          (GDestroyNotify) g_pattern_spec_free);
      model = G_LIST_MODEL (filter);
    }

  g_assert (model != NULL);
  g_assert (G_IS_LIST_MODEL (model));

  gtk_list_box_bind_model (self->list_box,
                           model,
                           editor_sidebar_create_row_cb,
                           self,
                           NULL);
}

static void
editor_sidebar_hide_cb (GtkWidget   *widget,
                        const gchar *action_name,
                        GVariant    *param)
{
  GtkWidget *window;

  g_assert (EDITOR_IS_SIDEBAR (widget));

  gtk_widget_hide (widget);

  if ((window = gtk_widget_get_ancestor (widget, EDITOR_TYPE_WINDOW)))
    {
      EditorPage *page = editor_window_get_visible_page (EDITOR_WINDOW (window));
      gtk_widget_grab_focus (GTK_WIDGET (page));
    }
}

static void
editor_sidebar_dispose (GObject *object)
{
  EditorSidebar *self = (EditorSidebar *)object;

  g_assert (EDITOR_IS_SIDEBAR (self));

  if (self->list_box != NULL)
    gtk_list_box_bind_model (self->list_box, NULL, NULL, NULL, NULL);

  g_clear_object (&self->model);
  g_clear_pointer ((GtkWidget **)&self->revealer, gtk_widget_unparent);

  G_OBJECT_CLASS (editor_sidebar_parent_class)->dispose (object);
}

static void
editor_sidebar_class_init (EditorSidebarClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_sidebar_dispose;

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "sidebar");
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-sidebar.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorSidebar, search_entry);
  gtk_widget_class_bind_template_child (widget_class, EditorSidebar, list_box);
  gtk_widget_class_bind_template_child (widget_class, EditorSidebar, revealer);
  gtk_widget_class_bind_template_child (widget_class, EditorSidebar, open_button);
  gtk_widget_class_bind_template_callback (widget_class, editor_sidebar_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, editor_sidebar_search_entry_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, editor_sidebar_search_entry_changed_cb);

  gtk_widget_class_install_action (widget_class, "sidebar.hide", NULL, editor_sidebar_hide_cb);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Escape, 0, "sidebar.hide", NULL);
}

static void
editor_sidebar_init (EditorSidebar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  g_signal_connect_object (self->revealer,
                           "notify::child-revealed",
                           G_CALLBACK (editor_sidebar_notify_child_revealed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  self->model = _editor_sidebar_model_new ();

  gtk_list_box_bind_model (self->list_box,
                           G_LIST_MODEL (self->model),
                           editor_sidebar_create_row_cb,
                           self,
                           NULL);
}

void
editor_sidebar_focus_search (EditorSidebar *self)
{
  g_return_if_fail (EDITOR_IS_SIDEBAR (self));

  gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

void
_editor_sidebar_page_reordered (EditorSidebar *self,
                                EditorPage    *page,
                                guint          page_num)
{
  g_return_if_fail (EDITOR_IS_SIDEBAR (self));
  g_return_if_fail (EDITOR_IS_PAGE (page));

  _editor_sidebar_model_page_reordered (self->model, page, page_num);
}
