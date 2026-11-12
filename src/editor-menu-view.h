/* editor-menu-view.h
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_MENU_VIEW (editor_menu_view_get_type())

G_DECLARE_FINAL_TYPE (EditorMenuView, editor_menu_view, EDITOR, MENU_VIEW, GtkWidget)

typedef GtkWidget *(*EditorMenuViewCreateCustomFunc) (const char *id,
                                                     gpointer    user_data);

GMenuModel *editor_menu_view_dup_menu_model         (EditorMenuView                 *self);
GMenuModel *editor_menu_view_get_menu_model         (EditorMenuView                 *self);
GtkWidget  *editor_menu_view_new                    (const char                     *title);
gboolean    editor_menu_view_pop                    (EditorMenuView                 *self);
void        editor_menu_view_set_create_custom_func (EditorMenuView                 *self,
                                                     EditorMenuViewCreateCustomFunc  func,
                                                     gpointer                        user_data);
void        editor_menu_view_set_menu_model         (EditorMenuView                 *self,
                                                     GMenuModel                     *menu_model);

G_END_DECLS
