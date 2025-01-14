/*
 * editor-document-operation.h
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

#define EDITOR_TYPE_DOCUMENT_OPERATION (editor_document_operation_get_type())

typedef enum _EditorDocumentOperationKind
{
  EDITOR_DOCUMENT_OPERATION_OPEN,
  EDITOR_DOCUMENT_OPERATION_AUTOSAVE,
  EDITOR_DOCUMENT_OPERATION_SAVE,
  EDITOR_DOCUMENT_OPERATION_SAVE_AS,
  EDITOR_DOCUMENT_OPERATION_SAVE_COPY,
  EDITOR_DOCUMENT_OPERATION_DISCARD_CHANGES,
  EDITOR_DOCUMENT_OPERATION_PRINT,
} EditorDocumentOperationKind;

typedef struct _EditorDocumentOperation EditorDocumentOperation;

GType                    editor_document_operation_get_type (void) G_GNUC_CONST;
EditorDocumentOperation *editor_document_operation_new      (EditorDocumentOperationKind  kind,
                                                             GFile                       *file,
                                                             EditorDraft                 *draft,
                                                             EditorSettings              *settings,
                                                             EditorProgress              *progress);
void                     editor_document_operation_run      (EditorDocumentOperation     *self,
                                                             EditorDocument              *document);
EditorDocumentOperation *editor_document_operation_ref      (EditorDocumentOperation     *self);
void                     editor_document_operation_unref    (EditorDocumentOperation     *self);
DexFuture               *editor_document_operation_await    (EditorDocumentOperation     *self);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (EditorDocumentOperation, editor_document_operation_unref)

G_END_DECLS
