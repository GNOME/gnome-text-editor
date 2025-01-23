/*
 * editor-encoding-model.h
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

#define EDITOR_TYPE_ENCODING (editor_encoding_get_type())
#define EDITOR_TYPE_ENCODING_MODEL (editor_encoding_model_get_type())

G_DECLARE_FINAL_TYPE (EditorEncoding, editor_encoding, EDITOR, ENCODING, GObject)
G_DECLARE_FINAL_TYPE (EditorEncodingModel, editor_encoding_model, EDITOR, ENCODING_MODEL, GObject)

EditorEncodingModel *editor_encoding_model_new            (void);
guint                editor_encoding_model_lookup         (EditorEncodingModel     *self,
                                                           EditorEncoding          *encoding);
guint                editor_encoding_model_lookup_charset (EditorEncodingModel     *self,
                                                           const char              *charset);
EditorEncoding      *editor_encoding_model_get            (EditorEncodingModel     *self,
                                                           const GtkSourceEncoding *encoding);

const GtkSourceEncoding *editor_encoding_get_encoding (EditorEncoding *self);
char                    *editor_encoding_dup_charset  (EditorEncoding *self);
char                    *editor_encoding_dup_name     (EditorEncoding *self);
guint                    editor_encoding_get_index    (EditorEncoding *self);

G_END_DECLS
