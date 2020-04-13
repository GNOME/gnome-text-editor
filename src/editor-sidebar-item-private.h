/* editor-sidebar-item-private.h
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

#define EDITOR_TYPE_SIDEBAR_ITEM (editor_sidebar_item_get_type())

G_DECLARE_FINAL_TYPE (EditorSidebarItem, editor_sidebar_item, EDITOR, SIDEBAR_ITEM, GObject)

EditorSidebarItem *_editor_sidebar_item_new             (GFile             *file,
                                                         EditorPage        *page);
gboolean           _editor_sidebar_item_get_empty       (EditorSidebarItem *self);
GFile             *_editor_sidebar_item_get_file        (EditorSidebarItem *self);
EditorPage        *_editor_sidebar_item_get_page        (EditorSidebarItem *self);
void               _editor_sidebar_item_set_page        (EditorSidebarItem *self,
                                                         EditorPage        *page);
gboolean           _editor_sidebar_item_get_is_modified (EditorSidebarItem *self);
gchar             *_editor_sidebar_item_dup_title       (EditorSidebarItem *self);
gchar             *_editor_sidebar_item_dup_subtitle    (EditorSidebarItem *self);
void               _editor_sidebar_item_open            (EditorSidebarItem *self);
gboolean           _editor_sidebar_item_matches         (EditorSidebarItem *self,
                                                         GPatternSpec      *spec);

G_END_DECLS
