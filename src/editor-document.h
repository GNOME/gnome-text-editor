/* editor-document.h
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

#include <libspelling.h>

#include "editor-types.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_DOCUMENT (editor_document_get_type())

G_DECLARE_FINAL_TYPE (EditorDocument, editor_document, EDITOR, DOCUMENT, GtkSourceBuffer)

EditorDocument           *editor_document_new_draft               (void);
EditorDocument           *editor_document_new_for_file            (GFile           *file);
gboolean                  editor_document_get_busy                (EditorDocument  *self);
gdouble                   editor_document_get_busy_progress       (EditorDocument  *self);
GFile                    *editor_document_get_file                (EditorDocument  *self);
gchar                    *editor_document_dup_title               (EditorDocument  *self);
gboolean                  editor_document_get_externally_modified (EditorDocument  *self);
SpellingChecker          *editor_document_get_spell_checker       (EditorDocument  *self);
void                      editor_document_set_spell_checker       (EditorDocument  *self,
                                                                   SpellingChecker *spell_checker);
void                      editor_document_update_corrections      (EditorDocument  *self);
GMenuModel               *editor_document_get_spelling_menu       (EditorDocument  *self);
EditorDocumentStatistics *editor_document_load_statistics         (EditorDocument  *self);
EditorEncoding           *editor_document_dup_encoding            (EditorDocument  *self);
void                      editor_document_set_encoding            (EditorDocument  *self,
                                                                   EditorEncoding  *encoding);

G_END_DECLS
