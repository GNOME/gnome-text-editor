/*
 * editor-document.h
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

#include <libdex.h>

#include "editor-types.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_DOCUMENT (editor_document_get_type())

G_DECLARE_FINAL_TYPE (EditorDocument, editor_document, EDITOR, DOCUMENT, GObject)

DexFuture          *editor_document_new             (EditorDraft    *draft,
                                                     GFile          *file);
DexFuture          *editor_document_open            (EditorDocument *self,
                                                     EditorProgress *progress);
DexFuture          *editor_document_print           (EditorDocument *self,
                                                     EditorProgress *progress);
DexFuture          *editor_document_autosave        (EditorDocument *self,
                                                     EditorProgress *progress);
DexFuture          *editor_document_save            (EditorDocument *self,
                                                     EditorProgress *progress);
DexFuture          *editor_document_save_as         (EditorDocument *self,
                                                     GFile          *file,
                                                     EditorProgress *progress);
DexFuture          *editor_document_save_copy       (EditorDocument *self,
                                                     GFile          *file,
                                                     EditorProgress *progress);
DexFuture          *editor_document_discard_changes (EditorDocument *self,
                                                     EditorProgress *progress);
GFile              *editor_document_dup_file        (EditorDocument *self);
EditorDraft        *editor_document_dup_draft       (EditorDocument *self);
EditorSettings     *editor_document_dup_settings    (EditorDocument *self);
EditorSourceBuffer *editor_document_dup_buffer      (EditorDocument *self);
gboolean            editor_document_can_discard     (EditorDocument *self);

G_END_DECLS
