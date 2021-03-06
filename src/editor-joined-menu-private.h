/* editor-joined-menu-private.h
 *
 * Copyright 2017-2021 Christian Hergert <chergert@redhat.com>
 *
 * This file is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License as published by the Free
 * Software Foundation; either version 3 of the License, or (at your option)
 * any later version.
 *
 * This file is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */

#pragma once

#include <gio/gio.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_JOINED_MENU (editor_joined_menu_get_type())

G_DECLARE_FINAL_TYPE (EditorJoinedMenu, editor_joined_menu, EDITOR, JOINED_MENU, GMenuModel)

EditorJoinedMenu *editor_joined_menu_new          (void);
guint             editor_joined_menu_get_n_joined (EditorJoinedMenu *self);
void              editor_joined_menu_append_menu  (EditorJoinedMenu *self,
                                                   GMenuModel      *model);
void              editor_joined_menu_prepend_menu (EditorJoinedMenu *self,
                                                   GMenuModel      *model);
void              editor_joined_menu_remove_menu  (EditorJoinedMenu *self,
                                                   GMenuModel      *model);
void              editor_joined_menu_remove_index (EditorJoinedMenu *self,
                                                   guint            index);

G_END_DECLS
