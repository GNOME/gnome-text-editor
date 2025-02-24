/* editor-open-view-private.h
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

#pragma once

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_OPEN_VIEW (editor_open_view_get_type())

G_DECLARE_FINAL_TYPE (EditorOpenView, editor_open_view, EDITOR, OPEN_VIEW, GtkWidget)

GtkWidget  *_editor_open_view_new        (void);
GListModel *_editor_open_view_get_model  (EditorOpenView *self);
void        _editor_open_view_set_model  (EditorOpenView *self,
                                          GListModel     *model);
gboolean    _editor_open_view_get_narrow (EditorOpenView *self);
void        _editor_open_view_set_narrow (EditorOpenView *self,
                                          gboolean        narrow);

G_END_DECLS
