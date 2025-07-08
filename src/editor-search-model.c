/*
 * editor-search-model.c
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

#include "editor-search-model.h"
#include "editor-sidebar-item-private.h"

/*
 * The goal of this object is to handle the search of sidebar model. We
 * want to avoid using custom sort/filter because we need to keep a score
 * of the items and potentially reorder the selection.
 *
 * We also want to do so with miminal changes to the rows so that we do
 * not unnecessarily create/update widgetry in response.
 *
 * See GNOME/gnome-text-editor#713 for why this is important.
 */

typedef struct _Item
{
  EditorSidebarItem *item;
  guint              score;
} Item;

struct _EditorSearchModel
{
  GObject     parent_instance;
  GListModel *model;
  GArray     *items;
  char       *search_text;
};

static GType
editor_search_model_get_item_type (GListModel *model)
{
  return G_TYPE_OBJECT;
}

static guint
editor_search_model_get_n_items (GListModel *model)
{
  return EDITOR_SEARCH_MODEL (model)->items->len;
}

static gpointer
editor_search_model_get_item (GListModel *model,
                              guint       position)
{
  EditorSearchModel *self = EDITOR_SEARCH_MODEL (model);

  if (position < self->items->len)
    return g_object_ref (g_array_index (self->items, Item, position).item);

  return NULL;
}

static void
list_model_iface_init (GListModelInterface *iface)
{
  iface->get_item_type = editor_search_model_get_item_type;
  iface->get_n_items = editor_search_model_get_n_items;
  iface->get_item = editor_search_model_get_item;
}

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorSearchModel, editor_search_model, G_TYPE_OBJECT,
                               G_IMPLEMENT_INTERFACE (G_TYPE_LIST_MODEL, list_model_iface_init))

enum {
  PROP_0,
  PROP_N_ITEMS,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
editor_search_model_change (EditorSearchModel *self,
                            GArray            *new_items)
{
  g_autoptr(GArray) old_items = NULL;

  g_assert (EDITOR_IS_SEARCH_MODEL (self));
  g_assert (new_items != NULL);

  old_items = g_steal_pointer (&self->items);
  self->items = new_items;

  /* NOTE: If this doesn't help reduce changes enough for GtkListModel
   * then we can look into handling shrinking better by emitting items-changed
   * multiple times for each run of removed items.
   *
   * Relevant for: GNOME/gnome-text-editor#713
   */

  g_list_model_items_changed (G_LIST_MODEL (self), 0, old_items->len, new_items->len);

  if (old_items->len != new_items->len)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_ITEMS]);
}

static void
clear_item (gpointer data)
{
  Item *item = data;

  g_clear_object (&item->item);
}

static void
editor_search_model_dispose (GObject *object)
{
  EditorSearchModel *self = (EditorSearchModel *)object;

  editor_search_model_set_model (self, NULL);

  G_OBJECT_CLASS (editor_search_model_parent_class)->dispose (object);
}

static void
editor_search_model_finalize (GObject *object)
{
  EditorSearchModel *self = (EditorSearchModel *)object;

  g_clear_pointer (&self->items, g_array_unref);
  g_clear_pointer (&self->search_text, g_free);

  G_OBJECT_CLASS (editor_search_model_parent_class)->finalize (object);
}

static void
editor_search_model_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EditorSearchModel *self = EDITOR_SEARCH_MODEL (object);

  switch (prop_id)
    {
    case PROP_N_ITEMS:
      g_value_set_uint (value, g_list_model_get_n_items (G_LIST_MODEL (self)));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_search_model_class_init (EditorSearchModelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_search_model_dispose;
  object_class->finalize = editor_search_model_finalize;
  object_class->get_property = editor_search_model_get_property;

  properties[PROP_N_ITEMS] =
    g_param_spec_uint ("n-items", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_search_model_init (EditorSearchModel *self)
{
  self->items = g_array_new (FALSE, TRUE, sizeof (Item));
  g_array_set_clear_func (self->items, clear_item);
}

EditorSearchModel *
editor_search_model_new (void)
{
  return g_object_new (EDITOR_TYPE_SEARCH_MODEL, NULL);
}

static int
sort_items (gconstpointer a,
            gconstpointer b)
{
  const Item *item_a = a;
  const Item *item_b = b;

  if (item_a->score < item_b->score)
    return -1;
  else if (item_a->score > item_b->score)
    return 1;
  else
    return 0;
}

static void
editor_search_model_refilter (EditorSearchModel *self)
{
  GArray *items;
  guint n_items;

  g_assert (EDITOR_IS_SEARCH_MODEL (self));

  if (self->model != NULL)
    n_items = g_list_model_get_n_items (self->model);
  else
    n_items = 0;

  items = g_array_sized_new (FALSE, TRUE, sizeof (Item), n_items);
  g_array_set_clear_func (items, clear_item);

  if (self->model != NULL)
    {
      gpointer itemptr;
      guint i = 0;

      while ((itemptr = g_list_model_get_item (self->model, i++)))
        {
          g_autoptr(EditorSidebarItem) item = itemptr;
          guint score = 0;

          if (_editor_sidebar_item_matches (item, self->search_text, &score))
            {
              Item itemval;

              itemval.score = score;
              itemval.item = g_steal_pointer (&item);

              g_array_append_val (items, itemval);
            }
        }
    }

  g_array_sort (items, sort_items);

  editor_search_model_change (self, items);
}

void
editor_search_model_set_model (EditorSearchModel *self,
                               GListModel        *model)
{
  g_return_if_fail (EDITOR_IS_SEARCH_MODEL (self));
  g_return_if_fail (!model || G_IS_LIST_MODEL (model));

  if (model == self->model)
    return;

  if (self->model)
    {
      g_signal_handlers_disconnect_by_func (self->model,
                                            G_CALLBACK (editor_search_model_refilter),
                                            self);
      g_clear_object (&self->model);
    }

  if (model)
    {
      self->model = g_object_ref (model);
      g_signal_connect_object (model,
                               "items-changed",
                               G_CALLBACK (editor_search_model_refilter),
                               self,
                               G_CONNECT_SWAPPED);
    }

  editor_search_model_refilter (self);
}

void
editor_search_model_set_search_text (EditorSearchModel *self,
                                     const char        *search_text)
{
  g_return_if_fail (EDITOR_IS_SEARCH_MODEL (self));

  if (search_text != NULL && search_text[0] == 0)
    search_text = 0;

  if (g_set_str (&self->search_text, search_text))
    editor_search_model_refilter (self);
}
