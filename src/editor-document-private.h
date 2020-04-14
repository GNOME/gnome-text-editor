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

EditorDocument *_editor_document_new            (GFile                *file,
                                                 const gchar          *draft_id);
const gchar    *_editor_document_get_draft_id   (EditorDocument       *self);
void            _editor_document_set_draft_id   (EditorDocument       *self,
                                                 const gchar          *draft_id);
GFile          *_editor_document_get_draft_file (EditorDocument       *self);
gchar          *_editor_document_dup_title      (EditorDocument       *self);
gchar          *_editor_document_dup_uri        (EditorDocument       *self);
void            _editor_document_mark_busy      (EditorDocument       *self);
void            _editor_document_unmark_busy    (EditorDocument       *self);
void            _editor_document_load_async     (EditorDocument       *self,
                                                 GCancellable         *cancellable,
                                                 GAsyncReadyCallback   callback,
                                                 gpointer              user_data);
gboolean        _editor_document_load_finish    (EditorDocument       *self,
                                                 GAsyncResult         *result,
                                                 GError              **error);

G_END_DECLS
