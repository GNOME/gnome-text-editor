/* editor-filter-model.h
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

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_FILTER_MODEL (editor_filter_model_get_type())

typedef gboolean (*EditorFilterModelFunc) (GObject  *object,
                                           gpointer  user_data);

G_DECLARE_FINAL_TYPE (EditorFilterModel, editor_filter_model, EDITOR, FILTER_MODEL, GObject)

EditorFilterModel *_editor_filter_model_new             (GListModel            *child_model);
GListModel        *_editor_filter_model_get_child_model (EditorFilterModel     *self);
void               _editor_filter_model_invalidate      (EditorFilterModel     *self);
void               _editor_filter_model_set_filter_func (EditorFilterModel     *self,
                                                         EditorFilterModelFunc  filter_func,
                                                         gpointer               filter_func_data,
                                                         GDestroyNotify         filter_func_data_destroy);

G_END_DECLS
