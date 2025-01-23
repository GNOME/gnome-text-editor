/*
 * editor-encoding-model.c
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

#include "editor-encoding-model.h"

struct _EditorEncoding
{
  GObject                  parent_instance;
  const GtkSourceEncoding *encoding;
  guint                    index;
};

G_DEFINE_FINAL_TYPE (EditorEncoding, editor_encoding, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_CHARSET,
  PROP_NAME,
  N_PROPS
};

static GObject *singleton;
static GParamSpec *properties[N_PROPS];

static void
editor_encoding_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EditorEncoding *self = EDITOR_ENCODING (object);

  switch (prop_id)
    {
    case PROP_CHARSET:
      g_value_take_string (value, editor_encoding_dup_charset (self));
      break;

    case PROP_NAME:
      g_value_take_string (value, editor_encoding_dup_name (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_encoding_class_init (EditorEncodingClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = editor_encoding_get_property;

  properties[PROP_CHARSET] =
    g_param_spec_string ("charset", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_NAME] =
    g_param_spec_string ("name", NULL, NULL,
                         NULL,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_encoding_init (EditorEncoding *self)
{
}

guint
editor_encoding_get_index (EditorEncoding *self)
{
  return self->index;
}

const GtkSourceEncoding *
editor_encoding_get_encoding (EditorEncoding *self)
{
  return self->encoding;
}

char *
editor_encoding_dup_charset (EditorEncoding *self)
{
  if (self->encoding)
    return g_strdup (gtk_source_encoding_get_charset (self->encoding));

  return NULL;
}

char *
editor_encoding_dup_name (EditorEncoding *self)
{
  if (self->encoding)
    return gtk_source_encoding_to_string (self->encoding);

  return NULL;
}

struct _EditorEncodingModel
{
  GObject    parent_instance;
  GPtrArray *items;
};

static guint
editor_encoding_model_get_n_items (GListModel *model)
{
  return EDITOR_ENCODING_MODEL (model)->items->len;
}

static GType
editor_encoding_model_get_item_type (GListModel *model)
{
  return EDITOR_TYPE_ENCODING;
}

static gpointer
editor_encoding_model_get_item (GListModel *model,
                                guint       position)
{
  if (position < EDITOR_ENCODING_MODEL (model)->items->len)
    return g_object_ref (g_ptr_array_index (EDITOR_ENCODING_MODEL (model)->items, position));

  return NULL;
}

static void
list_model_iface (GListModelInterface *iface)
{
  iface->get_n_items = editor_encoding_model_get_n_items;
  iface->get_item = editor_encoding_model_get_item;
  iface->get_item_type = editor_encoding_model_get_item_type;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorEncodingModel, editor_encoding_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface))

static void
editor_encoding_model_finalize (GObject *object)
{
  EditorEncodingModel *self = (EditorEncodingModel *)object;

  g_clear_pointer (&self->items, g_ptr_array_unref);

  G_OBJECT_CLASS (editor_encoding_model_parent_class)->finalize (object);
}

static GObject *
editor_encoding_model_constructor (GType                  type,
                                   guint                  n_construct_params,
                                   GObjectConstructParam *param)
{
  if (singleton == NULL)
    {
      singleton = G_OBJECT_CLASS (editor_encoding_model_parent_class)->constructor (type, n_construct_params, param);
      g_object_add_weak_pointer (singleton, (gpointer *)&singleton);
    }

  return g_object_ref (singleton);
}

static void
editor_encoding_model_class_init (EditorEncodingModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructor = editor_encoding_model_constructor;
  object_class->finalize = editor_encoding_model_finalize;
}

static int
compare_encoding (gconstpointer a,
                  gconstpointer b)
{
  const GtkSourceEncoding *e1 = a;
  const GtkSourceEncoding *e2 = b;

  return g_strcmp0 (gtk_source_encoding_get_name (e1),
                    gtk_source_encoding_get_name (e2));
}

static void
editor_encoding_model_init (EditorEncodingModel *self)
{
  GSList *all = gtk_source_encoding_get_all ();

  all = g_slist_sort (all, compare_encoding);

  self->items = g_ptr_array_new_with_free_func (g_object_unref);

  for (const GSList *iter = all; iter; iter = iter->next)
    {
      EditorEncoding *item = g_object_new (EDITOR_TYPE_ENCODING, NULL);
      item->encoding = iter->data;
      item->index = self->items->len;
      g_ptr_array_add (self->items, item);
    }

  g_slist_free (all);
}

EditorEncodingModel *
editor_encoding_model_new (void)
{
  return g_object_new (EDITOR_TYPE_ENCODING_MODEL, NULL);
}

static EditorEncodingModel *
editor_encoding_model_get_default (void)
{
  if (singleton == NULL)
    g_object_unref (editor_encoding_model_new ());

  return EDITOR_ENCODING_MODEL (singleton);
}

EditorEncoding *
editor_encoding_model_get (EditorEncodingModel     *self,
                           const GtkSourceEncoding *encoding)
{
  const char *charset;
  guint position;

  if (self == NULL)
    self = editor_encoding_model_get_default ();

  g_return_val_if_fail (EDITOR_IS_ENCODING_MODEL (self), NULL);

  if (encoding == NULL)
    encoding = gtk_source_encoding_get_utf8 ();

  charset = gtk_source_encoding_get_charset (encoding);
  position = editor_encoding_model_lookup_charset (self, charset);

  if (position == GTK_INVALID_LIST_POSITION || position >= self->items->len)
    return NULL;

  return g_ptr_array_index (self->items, position);
}

guint
editor_encoding_model_lookup (EditorEncodingModel *self,
                              EditorEncoding      *encoding)
{
  if (self == NULL)
    self = editor_encoding_model_get_default ();

  g_return_val_if_fail (EDITOR_IS_ENCODING_MODEL (self), GTK_INVALID_LIST_POSITION);

  for (guint i = 0; i < self->items->len; i++)
    {
      if (g_ptr_array_index (self->items, i) == encoding)
        return i;
    }

  return GTK_INVALID_LIST_POSITION;
}

guint
editor_encoding_model_lookup_charset (EditorEncodingModel *self,
                                      const char          *charset)
{
  if (self == NULL)
    self = editor_encoding_model_get_default ();

  g_return_val_if_fail (EDITOR_IS_ENCODING_MODEL (self), GTK_INVALID_LIST_POSITION);

  for (guint i = 0; i < self->items->len; i++)
    {
      EditorEncoding *encoding = g_ptr_array_index (self->items, i);
      g_autofree char *encoding_charset = editor_encoding_dup_charset (encoding);

      if (g_strcmp0 (encoding_charset, charset) == 0)
        return i;
    }

  return GTK_INVALID_LIST_POSITION;
}
