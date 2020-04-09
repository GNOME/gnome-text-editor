/* editor-binding-group.h
 *
 * Copyright (C) 2015 Christian Hergert <christian@hergert.me>
 * Copyright (C) 2015 Garrett Regier <garrettregier@gmail.com>
 *
 * This file is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1 of the
 * License, or (at your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_BINDING_GROUP (editor_binding_group_get_type())

G_DECLARE_FINAL_TYPE (EditorBindingGroup, editor_binding_group, EDITOR, BINDING_GROUP, GObject)

EditorBindingGroup *editor_binding_group_new                (void);
GObject            *editor_binding_group_get_source         (EditorBindingGroup    *self);
void                editor_binding_group_set_source         (EditorBindingGroup    *self,
                                                             gpointer               source);
void                editor_binding_group_bind               (EditorBindingGroup    *self,
                                                             const gchar           *source_property,
                                                             gpointer               target,
                                                             const gchar           *target_property,
                                                             GBindingFlags          flags);
void                editor_binding_group_bind_full          (EditorBindingGroup    *self,
                                                             const gchar           *source_property,
                                                             gpointer               target,
                                                             const gchar           *target_property,
                                                             GBindingFlags          flags,
                                                             GBindingTransformFunc  transform_to,
                                                             GBindingTransformFunc  transform_from,
                                                             gpointer               user_data,
                                                             GDestroyNotify         user_data_destroy);
void                editor_binding_group_bind_with_closures (EditorBindingGroup    *self,
                                                             const gchar           *source_property,
                                                             gpointer               target,
                                                             const gchar           *target_property,
                                                             GBindingFlags          flags,
                                                             GClosure              *transform_to,
                                                             GClosure              *transform_from);

G_END_DECLS
