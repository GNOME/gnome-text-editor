/*
 * editor-indent-model.c
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include <glib/gi18n.h>

#include "editor-indent-model.h"

struct _EditorIndent
{
  GObject parent_instance;
  const char *name;
  guint insert_spaces_instead_of_tabs : 1;
};

G_DEFINE_FINAL_TYPE (EditorIndent, editor_indent, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_NAME,
  PROP_INSERT_SPACES_INSTEAD_OF_TABS,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
editor_indent_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  EditorIndent *self = EDITOR_INDENT (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_take_string (value, editor_indent_dup_name (self));
      break;

    case PROP_INSERT_SPACES_INSTEAD_OF_TABS:
      g_value_set_boolean (value, editor_indent_get_insert_spaces_instead_of_tabs (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_indent_class_init (EditorIndentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = editor_indent_get_property;

  properties[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_INSERT_SPACES_INSTEAD_OF_TABS] =
    g_param_spec_boolean ("insert-spaces-instead-of-tabs", NULL, NULL,
                          FALSE,
                          (G_PARAM_READABLE |
                           G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_indent_init (EditorIndent *self)
{
}

gboolean
editor_indent_get_insert_spaces_instead_of_tabs (EditorIndent *self)
{
  return self->insert_spaces_instead_of_tabs;
}

char *
editor_indent_dup_name (EditorIndent *self)
{
  return g_strdup (self->name);
}

struct _EditorIndentModel
{
  GObject    parent_instance;
  GPtrArray *items;
};

static guint
editor_indent_model_get_n_items (GListModel *model)
{
  return EDITOR_INDENT_MODEL (model)->items->len;
}

static GType
editor_indent_model_get_item_type (GListModel *model)
{
  return EDITOR_TYPE_INDENT;
}

static gpointer
editor_indent_model_get_item (GListModel *model,
                              guint       position)
{
  if (position < EDITOR_INDENT_MODEL (model)->items->len)
    return g_object_ref (g_ptr_array_index (EDITOR_INDENT_MODEL (model)->items, position));

  return NULL;
}

static void
list_model_iface (GListModelInterface *iface)
{
  iface->get_n_items = editor_indent_model_get_n_items;
  iface->get_item = editor_indent_model_get_item;
  iface->get_item_type = editor_indent_model_get_item_type;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorIndentModel, editor_indent_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface))

static void
editor_indent_model_finalize (GObject *object)
{
  EditorIndentModel *self = (EditorIndentModel *)object;

  g_clear_pointer (&self->items, g_ptr_array_unref);

  G_OBJECT_CLASS (editor_indent_model_parent_class)->finalize (object);
}

static void
editor_indent_model_class_init (EditorIndentModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = editor_indent_model_finalize;
}

static void
editor_indent_model_init (EditorIndentModel *self)
{
  EditorIndent *indent;

  self->items = g_ptr_array_new_with_free_func (g_object_unref);

  indent = g_object_new (EDITOR_TYPE_INDENT, NULL);
  indent->name = _("Space");
  indent->insert_spaces_instead_of_tabs = TRUE;
  g_ptr_array_add (self->items, indent);

  indent = g_object_new (EDITOR_TYPE_INDENT, NULL);
  indent->name = _("Tab");
  indent->insert_spaces_instead_of_tabs = FALSE;
  g_ptr_array_add (self->items, indent);
}

guint
editor_indent_get_index_for (gboolean insert_spaces)
{
  return insert_spaces ? 0 : 1;
}
