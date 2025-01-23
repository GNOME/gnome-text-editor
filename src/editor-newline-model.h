/*
 * editor-newline-model.h
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

#pragma once

#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_NEWLINE (editor_newline_get_type())
#define EDITOR_TYPE_NEWLINE_MODEL (editor_newline_model_get_type())

G_DECLARE_FINAL_TYPE (EditorNewline, editor_newline, EDITOR, NEWLINE, GObject)
G_DECLARE_FINAL_TYPE (EditorNewlineModel, editor_newline_model, EDITOR, NEWLINE_MODEL, GObject)

EditorNewlineModel *editor_newline_model_new         (void);
guint               editor_newline_model_lookup      (EditorNewlineModel     *self,
                                                      EditorNewline          *newline);
EditorNewline      *editor_newline_model_get         (EditorNewlineModel     *self,
                                                      GtkSourceNewlineType    newline_type);

GtkSourceNewlineType  editor_newline_get_newline_type (EditorNewline *self);
char                 *editor_newline_dup_name         (EditorNewline *self);
guint                 editor_newline_get_index        (EditorNewline *self);

G_END_DECLS
