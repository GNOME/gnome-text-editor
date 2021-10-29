/* editor-open-popover.c
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

#define G_LOG_DOMAIN "editor-open-popover"

#include "config.h"

#include "editor-application.h"
#include "editor-open-popover-private.h"
#include "editor-session-private.h"
#include "editor-sidebar-item-private.h"
#include "editor-sidebar-row-private.h"
#include "editor-window.h"

struct _EditorOpenPopover
{
  GtkPopover      parent_instance;

  GListModel     *model;
  GListModel     *filtered_model;

  GtkListBox     *list_box;
  GtkWidget      *box;
  GtkSearchEntry *search_entry;
  GtkStack       *stack;
  GtkWidget      *empty;
  GtkWidget      *recent;
};

G_DEFINE_TYPE (EditorOpenPopover, editor_open_popover, GTK_TYPE_POPOVER)

enum {
  PROP_0,
  PROP_MODEL,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

GtkWidget *
_editor_open_popover_new (void)
{
  return g_object_new (EDITOR_TYPE_OPEN_POPOVER, NULL);
}

static GListModel *
editor_open_popover_get_model (EditorOpenPopover *self)
{
  g_assert (EDITOR_IS_OPEN_POPOVER (self));

  if (self->filtered_model)
    return self->filtered_model;

  return self->model;
}

static void
popover_hide (GtkWidget  *widget,
              const char *action_name,
              GVariant   *param)
{
  GtkWidget *parent;

  g_assert (GTK_IS_POPOVER (widget));

  parent = gtk_widget_get_parent (widget);
  gtk_menu_button_popdown (GTK_MENU_BUTTON (parent));
}

static GtkWidget *
create_row (gpointer itemptr,
            gpointer user_data)
{
  EditorSidebarItem *item = itemptr;

  g_assert (EDITOR_IS_SIDEBAR_ITEM (item));

  return g_object_new (EDITOR_TYPE_SIDEBAR_ROW,
                       "item", item,
                       NULL);
}

static void
on_list_box_row_activated_cb (EditorOpenPopover *self,
                              EditorSidebarRow  *row,
                              GtkListBox        *list_box)
{
  EditorSidebarItem *item;
  EditorWindow *window;

  g_assert (EDITOR_IS_OPEN_POPOVER (self));
  g_assert (EDITOR_IS_SIDEBAR_ROW (row));
  g_assert (GTK_IS_LIST_BOX (list_box));

  window = EDITOR_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));
  item = _editor_sidebar_row_get_item (row);

  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");
  _editor_sidebar_item_open (item, EDITOR_SESSION_DEFAULT, window);
  popover_hide (GTK_WIDGET (self), NULL, NULL);
}

static gboolean
editor_sidebar_filter_func_cb (gpointer itemptr,
                               gpointer user_data)
{
  EditorSidebarItem *item = itemptr;
  const char *search = user_data;

  return _editor_sidebar_item_matches (item, search);
}

static void
on_search_entry_changed_cb (EditorOpenPopover *self,
                            GtkSearchEntry    *search_entry)
{
  g_autoptr(GtkFilterListModel) filter = NULL;
  const gchar *text;
  GListModel *model;

  g_assert (EDITOR_IS_OPEN_POPOVER (self));
  g_assert (GTK_IS_SEARCH_ENTRY (search_entry));

  text = gtk_editable_get_text (GTK_EDITABLE (search_entry));

  if (text == NULL || text[0] == 0)
    {
      model = G_LIST_MODEL (self->model);
    }
  else
    {
      g_autofree gchar *text_fold = g_utf8_casefold (text, -1);
      g_autoptr(GtkCustomFilter) custom = NULL;

      custom = gtk_custom_filter_new (editor_sidebar_filter_func_cb,
                                      g_steal_pointer (&text_fold),
                                      g_free);
      filter = gtk_filter_list_model_new (g_object_ref (G_LIST_MODEL (self->model)),
                                          g_object_ref (GTK_FILTER (custom)));
      model = G_LIST_MODEL (filter);
    }

  g_assert (model != NULL);
  g_assert (G_IS_LIST_MODEL (model));

  gtk_list_box_bind_model (self->list_box, model, create_row, NULL, NULL);

  g_set_object (&self->filtered_model, model);
}

static void
on_search_entry_activate_cb (EditorOpenPopover *self,
                             GtkSearchEntry    *search_entry)
{
  GListModel *model;

  g_assert (EDITOR_IS_OPEN_POPOVER (self));
  g_assert (GTK_IS_SEARCH_ENTRY (search_entry));

  if ((model = editor_open_popover_get_model (self)) &&
      g_list_model_get_n_items (model) > 0)
    {
      g_autoptr(EditorSidebarItem) item = g_list_model_get_item (model, 0);
      EditorWindow *window;

      gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");

      window = EDITOR_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));
      _editor_sidebar_item_open (item, EDITOR_SESSION_DEFAULT, window);

      popover_hide (GTK_WIDGET (self), NULL, NULL);
    }
}

static void
on_search_entry_stop_search_cb (EditorOpenPopover *self,
                                GtkSearchEntry    *search_entry)
{
  EditorWindow *window;

  g_assert (EDITOR_IS_OPEN_POPOVER (self));
  g_assert (GTK_IS_SEARCH_ENTRY (search_entry));

  window = EDITOR_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));

  gtk_popover_popdown (GTK_POPOVER (self));

  if (window != NULL)
    {
      EditorPage *page = editor_window_get_visible_page (window);

      gtk_widget_child_focus (GTK_WIDGET (page), GTK_DIR_TAB_FORWARD);
    }
}

static void
editor_open_popover_dispose (GObject *object)
{
  EditorOpenPopover *self = (EditorOpenPopover *)object;

  g_clear_pointer (&self->box, gtk_widget_unparent);
  g_clear_object (&self->model);
  g_clear_object (&self->filtered_model);

  G_OBJECT_CLASS (editor_open_popover_parent_class)->dispose (object);
}

static void
editor_open_popover_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EditorOpenPopover *self = EDITOR_OPEN_POPOVER (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, _editor_open_popover_get_model (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_open_popover_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EditorOpenPopover *self = EDITOR_OPEN_POPOVER (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      _editor_open_popover_set_model (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_open_popover_class_init (EditorOpenPopoverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_open_popover_dispose;
  object_class->get_property = editor_open_popover_get_property;
  object_class->set_property = editor_open_popover_set_property;

  properties [PROP_MODEL] =
    g_param_spec_object ("model",
                         "Model",
                         "The model to be loaded",
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-open-popover.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, box);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, empty);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, search_entry);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, list_box);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, recent);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, stack);
  gtk_widget_class_bind_template_callback (widget_class, on_list_box_row_activated_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_stop_search_cb);
}

static void
editor_open_popover_init (EditorOpenPopover *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GListModel *
_editor_open_popover_get_model (EditorOpenPopover *self)
{
  return self->model;
}

static void
editor_open_popover_items_changed_cb (EditorOpenPopover *self,
                                      guint              position,
                                      guint              removed,
                                      guint              added,
                                      GListModel        *model)
{
  GtkWidget *child;

  g_assert (EDITOR_IS_OPEN_POPOVER (self));
  g_assert (G_IS_LIST_MODEL (model));

  if (added || g_list_model_get_n_items (model))
    child = self->recent;
  else
    child = self->empty;

  if (child != gtk_stack_get_visible_child (self->stack))
    gtk_stack_set_visible_child (self->stack, child);
}

void
_editor_open_popover_set_model (EditorOpenPopover *self,
                                GListModel        *model)
{
  g_return_if_fail (EDITOR_IS_OPEN_POPOVER (self));
  g_return_if_fail (!model || G_IS_LIST_MODEL (model));

  if (g_set_object (&self->model, model))
    {
      g_clear_object (&self->filtered_model);

      if (model != NULL)
        {
          g_signal_connect_object (model,
                                   "items-changed",
                                   G_CALLBACK (editor_open_popover_items_changed_cb),
                                   self,
                                   G_CONNECT_SWAPPED);

          editor_open_popover_items_changed_cb (self, 0, 0, 0, model);
        }

      gtk_list_box_bind_model (self->list_box, model, create_row, NULL, NULL);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_MODEL]);
    }
}
