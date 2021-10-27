/* editor-sidebar-row.c
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

#define G_LOG_DOMAIN "editor-sidebar-row"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-path-private.h"
#include "editor-session-private.h"
#include "editor-sidebar-item-private.h"
#include "editor-sidebar-row-private.h"
#include "editor-utils-private.h"

struct _EditorSidebarRow
{
  GtkWidget          parent_instance;

  EditorSidebarItem *item;

  GtkWidget         *grid;
  GtkLabel          *title;
  GtkLabel          *subtitle;
  GtkLabel          *is_modified;
  GtkLabel          *empty;
  GtkLabel          *age;
  GtkStack          *stack;
  GtkButton         *remove;

  GBinding          *title_binding;
  GBinding          *subtitle_binding;
  GBinding          *empty_binding;
  GBinding          *modified_binding;
  GBinding          *age_binding;
};

enum {
  PROP_0,
  PROP_ITEM,
  N_PROPS
};

G_DEFINE_TYPE (EditorSidebarRow, editor_sidebar_row, GTK_TYPE_WIDGET)

static GParamSpec *properties [N_PROPS];

static gboolean
modified_to_child (GBinding     *binding,
                   const GValue *from_value,
                   GValue       *to_value,
                   gpointer      user_data)
{
  EditorSidebarRow *self = user_data;

  g_assert (G_IS_BINDING (binding));
  g_assert (EDITOR_IS_SIDEBAR_ROW (self));

  if (g_value_get_boolean (from_value))
    g_value_set_object (to_value, self->is_modified);
  else
    g_value_set_object (to_value, self->empty);

  return TRUE;
}

static gboolean
age_to_string (GBinding     *binding,
               const GValue *from_value,
               GValue       *to_value,
               gpointer      user_data)
{
  GDateTime *dt;

  g_assert (G_IS_BINDING (binding));
  g_assert (EDITOR_IS_SIDEBAR_ROW (user_data));

  if ((dt = g_value_get_boxed (from_value)))
    g_value_take_string (to_value,
                         _editor_date_time_format (dt));
  else
    g_value_set_static_string (to_value, "");

  return TRUE;
}

static void
editor_sidebar_row_dispose (GObject *object)
{
  EditorSidebarRow *self = (EditorSidebarRow *)object;

  g_clear_pointer (&self->grid, gtk_widget_unparent);
  g_clear_pointer (&self->title_binding, g_binding_unbind);
  g_clear_pointer (&self->subtitle_binding, g_binding_unbind);
  g_clear_pointer (&self->empty_binding, g_binding_unbind);
  g_clear_pointer (&self->modified_binding, g_binding_unbind);
  g_clear_pointer (&self->age_binding, g_binding_unbind);
  g_clear_object (&self->item);

  G_OBJECT_CLASS (editor_sidebar_row_parent_class)->dispose (object);
}

static void
editor_sidebar_row_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  EditorSidebarRow *self = EDITOR_SIDEBAR_ROW (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      g_value_set_object (value, _editor_sidebar_row_get_item (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_sidebar_row_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  EditorSidebarRow *self = EDITOR_SIDEBAR_ROW (object);

  switch (prop_id)
    {
    case PROP_ITEM:
      _editor_sidebar_row_set_item (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_sidebar_row_class_init (EditorSidebarRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_sidebar_row_dispose;
  object_class->get_property = editor_sidebar_row_get_property;
  object_class->set_property = editor_sidebar_row_set_property;

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-sidebar-row.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_bind_template_child (widget_class, EditorSidebarRow, age);
  gtk_widget_class_bind_template_child (widget_class, EditorSidebarRow, empty);
  gtk_widget_class_bind_template_child (widget_class, EditorSidebarRow, grid);
  gtk_widget_class_bind_template_child (widget_class, EditorSidebarRow, is_modified);
  gtk_widget_class_bind_template_child (widget_class, EditorSidebarRow, remove);
  gtk_widget_class_bind_template_child (widget_class, EditorSidebarRow, stack);
  gtk_widget_class_bind_template_child (widget_class, EditorSidebarRow, subtitle);
  gtk_widget_class_bind_template_child (widget_class, EditorSidebarRow, title);

  properties [PROP_ITEM] =
    g_param_spec_object ("item",
                         "Item",
                         "The item to be displayed",
                         EDITOR_TYPE_SIDEBAR_ITEM,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_sidebar_row_init (EditorSidebarRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
_editor_sidebar_row_new (void)
{
  return g_object_new (EDITOR_TYPE_SIDEBAR_ROW, NULL);
}

EditorSidebarItem *
_editor_sidebar_row_get_item (EditorSidebarRow *self)
{
  g_return_val_if_fail (EDITOR_IS_SIDEBAR_ROW (self), NULL);

  return self->item;
}

void
_editor_sidebar_row_set_item (EditorSidebarRow  *self,
                              EditorSidebarItem *item)
{
  g_autoptr(EditorSidebarItem) previous = NULL;

  g_return_if_fail (EDITOR_IS_SIDEBAR_ROW (self));

  if (self->item == item)
    return;

  previous = g_steal_pointer (&self->item);
  if (item != NULL)
    g_object_ref (item);

  g_clear_pointer (&self->title_binding, g_binding_unbind);
  g_clear_pointer (&self->subtitle_binding, g_binding_unbind);
  g_clear_pointer (&self->empty_binding, g_binding_unbind);
  g_clear_pointer (&self->modified_binding, g_binding_unbind);

  self->item = item;

  if (item != NULL)
    {
      GFile *file = _editor_sidebar_item_get_file (item);
      const char *draft_id = _editor_sidebar_item_get_draft_id (item);
      g_autofree char *uri = file ? g_file_get_uri (file) : NULL;

      self->title_binding =
        g_object_bind_property (self->item, "title",
                                self->title, "label",
                                G_BINDING_SYNC_CREATE);
      self->subtitle_binding =
        g_object_bind_property (self->item, "subtitle",
                                self->subtitle, "label",
                                G_BINDING_SYNC_CREATE);
      self->empty_binding =
        g_object_bind_property (self->item, "empty",
                                self, "visible",
                                G_BINDING_SYNC_CREATE | G_BINDING_INVERT_BOOLEAN);
      self->modified_binding =
        g_object_bind_property_full (self->item, "is-modified",
                                     self->stack, "visible-child",
                                     G_BINDING_SYNC_CREATE,
                                     modified_to_child,
                                     NULL, self, NULL);
      self->age_binding =
        g_object_bind_property_full (self->item, "age",
                                     self->age, "label",
                                     G_BINDING_SYNC_CREATE,
                                     age_to_string,
                                     NULL, self, NULL);
      gtk_actionable_set_action_target (GTK_ACTIONABLE (self->remove),
                                        "(ss)",
                                        uri ? uri : "",
                                        draft_id ? draft_id : "");
      gtk_actionable_set_action_name (GTK_ACTIONABLE (self->remove), "row.remove");
    }
}
