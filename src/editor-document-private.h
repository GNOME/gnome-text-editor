/* editor-document-private.h
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

#include "editor-document.h"

G_BEGIN_DECLS

EditorDocument           *_editor_document_new                     (GFile                    *file,
                                                                    const gchar              *draft_id);
const gchar              *_editor_document_get_draft_id            (EditorDocument           *self);
void                      _editor_document_set_draft_id            (EditorDocument           *self,
                                                                    const gchar              *draft_id);
GFile                    *_editor_document_get_draft_file          (EditorDocument           *self);
gchar                    *_editor_document_dup_uri                 (EditorDocument           *self);
gboolean                  _editor_document_get_loading             (EditorDocument           *self);
void                      _editor_document_mark_busy               (EditorDocument           *self);
void                      _editor_document_unmark_busy             (EditorDocument           *self);
void                      _editor_document_set_externally_modified (EditorDocument           *self,
                                                                    gboolean                  externally_modified);
gboolean                  _editor_document_get_was_restored        (EditorDocument           *self);
void                      _editor_document_set_was_restored        (EditorDocument           *self,
                                                                    gboolean                  was_restored);
const GtkSourceEncoding  *_editor_document_get_encoding            (EditorDocument           *self);
void                      _editor_document_set_encoding            (EditorDocument           *document,
                                                                    const GtkSourceEncoding  *encoding);
GtkSourceNewlineType      _editor_document_get_newline_type        (EditorDocument           *self);
void                      _editor_document_set_newline_type        (EditorDocument           *self,
                                                                    GtkSourceNewlineType      newline_type);
void                      _editor_document_load_async              (EditorDocument           *self,
                                                                    EditorWindow             *window,
                                                                    GCancellable             *cancellable,
                                                                    GAsyncReadyCallback       callback,
                                                                    gpointer                  user_data);
gboolean                  _editor_document_load_finish             (EditorDocument           *self,
                                                                    GAsyncResult             *result,
                                                                    GError                  **error);
void                      _editor_document_save_async              (EditorDocument           *self,
                                                                    GFile                    *file,
                                                                    GCancellable             *cancellable,
                                                                    GAsyncReadyCallback       callback,
                                                                    gpointer                  user_data);
gboolean                  _editor_document_save_finish             (EditorDocument           *self,
                                                                    GAsyncResult             *result,
                                                                    GError                  **error);
void                      _editor_document_save_draft_async        (EditorDocument           *self,
                                                                    GCancellable             *cancellable,
                                                                    GAsyncReadyCallback       callback,
                                                                    gpointer                  user_data);
gboolean                  _editor_document_save_draft_finish       (EditorDocument           *self,
                                                                    GAsyncResult             *result,
                                                                    GError                  **error);
void                      _editor_document_guess_language_async    (EditorDocument           *self,
                                                                    GCancellable             *cancellable,
                                                                    GAsyncReadyCallback       callback,
                                                                    gpointer                  user_data);
gboolean                  _editor_document_guess_language_finish   (EditorDocument           *self,
                                                                    GAsyncResult             *result,
                                                                    GError                  **error);
void                      _editor_document_attach_actions          (EditorDocument           *self,
                                                                    GtkWidget                *widget);
GtkTextTag               *_editor_document_get_spelling_tag        (EditorDocument           *self);
void                      _editor_document_use_admin               (EditorDocument           *self,
                                                                    EditorWindow             *window);
gboolean                  _editor_document_had_error               (EditorDocument           *self);
void                      _editor_document_persist_syntax_language (EditorDocument           *self,
                                                                    const char               *language_id);
char                     *_editor_document_suggest_filename        (EditorDocument           *self);
GFile                    *_editor_document_suggest_file            (EditorDocument           *self,
                                                                    GFile                    *directory);
gboolean                  _editor_document_did_shutdown            (EditorDocument           *self);
void                      _editor_document_shutdown                (EditorDocument           *self);
void                      _editor_document_save_insert_mark        (EditorDocument           *self);
void                      _editor_document_restore_insert_mark     (EditorDocument           *self);

G_END_DECLS
