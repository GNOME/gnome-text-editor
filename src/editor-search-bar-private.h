/* editor-search-bar.h
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

#include "editor-types-private.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_SEARCH_BAR (editor_search_bar_get_type())

typedef enum
{
  EDITOR_SEARCH_BAR_MODE_SEARCH,
  EDITOR_SEARCH_BAR_MODE_REPLACE,
} EditorSearchBarMode;

G_DECLARE_FINAL_TYPE (EditorSearchBar, editor_search_bar, EDITOR, SEARCH_BAR, GtkWidget)

void     _editor_search_bar_attach              (EditorSearchBar     *self,
                                                 EditorDocument      *document);
void     _editor_search_bar_detach              (EditorSearchBar     *self);
void     _editor_search_bar_set_mode            (EditorSearchBar     *self,
                                                 EditorSearchBarMode  mode);
void     _editor_search_bar_move_next           (EditorSearchBar     *self);
void     _editor_search_bar_move_previous       (EditorSearchBar     *self);
gboolean _editor_search_bar_get_can_move        (EditorSearchBar     *self);
gboolean _editor_search_bar_get_can_replace     (EditorSearchBar     *self);
gboolean _editor_search_bar_get_can_replace_all (EditorSearchBar     *self);
void     _editor_search_bar_replace             (EditorSearchBar     *self);
void     _editor_search_bar_replace_all         (EditorSearchBar     *self);

G_END_DECLS
