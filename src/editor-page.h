/* editor-page.h
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

#include "editor-types.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_PAGE (editor_page_get_type())

G_DECLARE_FINAL_TYPE (EditorPage, editor_page, EDITOR, PAGE, GtkWidget)

EditorPage     *editor_page_new_for_document    (EditorDocument *document);
EditorDocument *editor_page_get_document        (EditorPage     *self);
gboolean        editor_page_get_busy            (EditorPage     *self);
gboolean        editor_page_get_can_discard     (EditorPage     *self);
gboolean        editor_page_get_can_save        (EditorPage     *self);
gboolean        editor_page_get_is_modified     (EditorPage     *self);
gboolean        editor_page_is_active           (EditorPage     *self);
gboolean        editor_page_is_draft            (EditorPage     *self);
void            editor_page_grab_focus          (EditorPage     *self);
gchar          *editor_page_dup_title           (EditorPage     *self);
gchar          *editor_page_dup_subtitle        (EditorPage     *self);
gchar          *editor_page_dup_position_label  (EditorPage     *self);
void            editor_page_get_visual_position (EditorPage     *self,
                                                 guint          *line,
                                                 guint          *column);
const char     *editor_page_get_language_name   (EditorPage     *self);
void            editor_page_destroy             (EditorPage     *self);

G_END_DECLS
