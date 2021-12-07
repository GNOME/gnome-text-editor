/* editor-statusbar-private.h
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

#include "editor-page.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_STATUSBAR (editor_statusbar_get_type())

G_DECLARE_FINAL_TYPE (EditorStatusbar, editor_statusbar, EDITOR, STATUSBAR, GtkWidget)

GtkWidget  *editor_statusbar_new                  (void);
void        editor_statusbar_bind_page            (EditorStatusbar *self,
                                                   EditorPage      *page);
void        editor_statusbar_set_selection_count  (EditorStatusbar *self,
                                                   guint            selection_lines);
void        editor_statusbar_set_position         (EditorStatusbar *self,
                                                   guint            line,
                                                   guint            line_offset);
const char *editor_statusbar_get_command_text     (EditorStatusbar *self);
void        editor_statusbar_set_command_text     (EditorStatusbar *self,
                                                   const char      *command_text);
const char *editor_statusbar_get_command_bar_text (EditorStatusbar *self);
void        editor_statusbar_set_command_bar_text (EditorStatusbar *self,
                                                   const char      *command_bar_text);
void        editor_statusbar_set_overwrite        (EditorStatusbar *self,
                                                   gboolean         overwrite);

G_END_DECLS
