/*
 * editor-properties.h
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

#include "editor-encoding-model.h"
#include "editor-newline-model.h"
#include "editor-types.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_PROPERTIES (editor_properties_get_type())

G_DECLARE_FINAL_TYPE (EditorProperties, editor_properties, EDITOR, PROPERTIES, GObject)

EditorProperties         *editor_properties_new               (void);
gboolean                  editor_properties_get_can_open      (EditorProperties  *self);
gboolean                  editor_properties_get_auto_indent   (EditorProperties  *self);
void                      editor_properties_set_auto_indent   (EditorProperties  *self,
                                                               gboolean           auto_indent);
GtkAdjustment            *editor_properties_dup_indent_width  (EditorProperties  *self);
GtkAdjustment            *editor_properties_dup_tab_width     (EditorProperties  *self);
EditorPage               *editor_properties_dup_page          (EditorProperties  *self);
void                      editor_properties_set_page          (EditorProperties  *self,
                                                               EditorPage        *page);
char                     *editor_properties_dup_name          (EditorProperties  *self);
char                     *editor_properties_dup_directory     (EditorProperties  *self);
GtkSourceLanguage        *editor_properties_dup_language      (EditorProperties  *self);
void                      editor_properties_set_language      (EditorProperties  *self,
                                                               GtkSourceLanguage *language);
char                     *editor_properties_dup_language_name (EditorProperties  *self);
EditorNewline            *editor_properties_dup_newline_type  (EditorProperties  *self);
void                      editor_properties_set_newline_type  (EditorProperties  *self,
                                                               EditorNewline     *newline);
EditorEncoding           *editor_properties_dup_encoding      (EditorProperties  *self);
void                      editor_properties_set_encoding      (EditorProperties  *self,
                                                               EditorEncoding    *encoding);
EditorDocumentStatistics *editor_properties_dup_statistics    (EditorProperties  *self);

G_END_DECLS
