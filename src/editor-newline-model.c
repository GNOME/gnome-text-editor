/*
 * editor-newline-model.c
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

#include "editor-newline-model.h"

struct _EditorNewline
{
  GObject parent_instance;
  GtkSourceNewlineType type;
  const char *value;
  const char *name;
  guint index;
};

G_DEFINE_FINAL_TYPE (EditorNewline, editor_newline, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_NAME,
  N_PROPS
};

static GObject *singleton;
static GParamSpec *properties[N_PROPS];

static void
editor_newline_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  EditorNewline *self = EDITOR_NEWLINE (object);

  switch (prop_id)
    {
    case PROP_NAME:
      g_value_take_string (value, editor_newline_dup_name (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_newline_class_init (EditorNewlineClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = editor_newline_get_property;

  properties[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_newline_init (EditorNewline *self)
{
}

GtkSourceNewlineType
editor_newline_get_newline_type (EditorNewline *self)
{
  return self->type;
}

char *
editor_newline_dup_name (EditorNewline *self)
{
  return g_strdup (self->name);
}

struct _EditorNewlineModel
{
  GObject    parent_instance;
  GPtrArray *items;
};

static guint
editor_newline_model_get_n_items (GListModel *model)
{
  return EDITOR_NEWLINE_MODEL (model)->items->len;
}

static GType
editor_newline_model_get_item_type (GListModel *model)
{
  return EDITOR_TYPE_NEWLINE;
}

static gpointer
editor_newline_model_get_item (GListModel *model,
                                guint       position)
{
  if (position < EDITOR_NEWLINE_MODEL (model)->items->len)
    return g_object_ref (g_ptr_array_index (EDITOR_NEWLINE_MODEL (model)->items, position));

  return NULL;
}

static void
list_model_iface (GListModelInterface *iface)
{
  iface->get_n_items = editor_newline_model_get_n_items;
  iface->get_item = editor_newline_model_get_item;
  iface->get_item_type = editor_newline_model_get_item_type;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorNewlineModel, editor_newline_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface))

static GObject *
editor_newline_model_constructor (GType                  type,
                                  guint                  n_construct_params,
                                  GObjectConstructParam *param)
{
  if (singleton == NULL)
    {
      singleton = G_OBJECT_CLASS (editor_newline_model_parent_class)->constructor (type, n_construct_params, param);
      g_object_add_weak_pointer (singleton, (gpointer *)&singleton);
    }

  return g_object_ref (singleton);
}

static void
editor_newline_model_finalize (GObject *object)
{
  EditorNewlineModel *self = (EditorNewlineModel *)object;

  g_clear_pointer (&self->items, g_ptr_array_unref);

  G_OBJECT_CLASS (editor_newline_model_parent_class)->finalize (object);
}

static void
editor_newline_model_class_init (EditorNewlineModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructor = editor_newline_model_constructor;
  object_class->finalize = editor_newline_model_finalize;
}

static void
editor_newline_model_init (EditorNewlineModel *self)
{
  const struct {
    GtkSourceNewlineType type;
    const char *value;
    const char *name;
  } items[] = {
    { GTK_SOURCE_NEWLINE_TYPE_LF, "\n", _("Unix/Linux (LF)") },
    { GTK_SOURCE_NEWLINE_TYPE_CR_LF, "\r\n", _("Windows (CR+LF)") },
    { GTK_SOURCE_NEWLINE_TYPE_CR, "\r", _("Mac OS Classic (CR)") },
  };

  self->items = g_ptr_array_new_with_free_func (g_object_unref);

  for (guint i = 0; i < G_N_ELEMENTS (items); i++)
    {
      EditorNewline *item = g_object_new (EDITOR_TYPE_NEWLINE, NULL);
      item->type = items[i].type;
      item->value = items[i].value;
      item->name = items[i].name;
      item->index = i;
      g_ptr_array_add (self->items, item);
    }
}

EditorNewlineModel *
editor_newline_model_new (void)
{
  return g_object_new (EDITOR_TYPE_NEWLINE_MODEL, NULL);
}

static EditorNewlineModel *
editor_newline_model_get_default (void)
{
  if (singleton == NULL)
    g_object_unref (editor_newline_model_new ());

  return EDITOR_NEWLINE_MODEL (singleton);
}

EditorNewline *
editor_newline_model_get (EditorNewlineModel   *self,
                          GtkSourceNewlineType  newline_type)
{
  if (self == NULL)
    self = editor_newline_model_get_default ();

  g_return_val_if_fail (EDITOR_IS_NEWLINE_MODEL (self), NULL);

  for (guint i = 0; i < self->items->len; i++)
    {
      EditorNewline *newline = g_ptr_array_index (self->items, i);

      if (newline_type == newline->type)
        return newline;
    }

  return NULL;
}

guint
editor_newline_model_lookup (EditorNewlineModel *self,
                             EditorNewline      *newline)
{
  if (newline != NULL)
    return newline->index;

  return GTK_INVALID_LIST_POSITION;
}

guint
editor_newline_get_index (EditorNewline *self)
{
  return self->index;
}
