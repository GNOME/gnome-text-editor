/*
 * editor-document-operation.c
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

#include "config.h"

#include <gtksourceview/gtksource.h>

#include "editor-document-operation.h"
#include "editor-document-private.h"
#include "editor-draft.h"
#include "editor-progress.h"
#include "editor-settings.h"
#include "editor-source-buffer.h"
#include "editor-source-file-loader.h"
#include "editor-source-file-saver.h"

struct _EditorDocumentOperation
{
  EditorDocumentOperationKind  kind;
  DexPromise                  *promise;
  EditorDraft                 *draft;
  EditorProgress              *progress;
  EditorSettings              *settings;
  GFile                       *file;
};

G_DEFINE_BOXED_TYPE (EditorDocumentOperation,
                     editor_document_operation,
                     editor_document_operation_ref,
                     editor_document_operation_unref)

EditorDocumentOperation *
editor_document_operation_new (EditorDocumentOperationKind  kind,
                               GFile                       *file,
                               EditorDraft                 *draft,
                               EditorSettings              *settings,
                               EditorProgress              *progress)
{
  EditorDocumentOperation *self;

  g_return_val_if_fail (!file || G_IS_FILE (file), NULL);
  g_return_val_if_fail (!draft || EDITOR_IS_DRAFT (draft), NULL);
  g_return_val_if_fail (!progress || EDITOR_IS_PROGRESS (progress), NULL);
  g_return_val_if_fail (EDITOR_IS_SETTINGS (settings), NULL);

  self = g_atomic_rc_box_new0 (EditorDocumentOperation);
  self->kind = kind;
  self->promise = dex_promise_new_cancellable ();

  g_set_object (&self->file, file);
  g_set_object (&self->progress, progress);
  g_set_object (&self->draft, draft);
  g_set_object (&self->settings, settings);

  if (self->progress == NULL)
    self->progress = editor_progress_new ();

  return self;
}

EditorDocumentOperation *
editor_document_operation_ref (EditorDocumentOperation *self)
{
  return g_atomic_rc_box_acquire (self);
}

static void
editor_document_operation_finalize (gpointer data)
{
  EditorDocumentOperation *self = data;

  g_clear_object (&self->file);
  g_clear_object (&self->draft);
  g_clear_object (&self->progress);
  g_clear_object (&self->settings);

  if (dex_future_is_pending (DEX_FUTURE (self->promise)))
    dex_promise_reject (self->promise,
                        g_error_new (G_IO_ERROR,
                                     G_IO_ERROR_CANCELLED,
                                     "Operation cancelled"));
}

void
editor_document_operation_unref (EditorDocumentOperation *self)
{
  return g_atomic_rc_box_release_full (self, editor_document_operation_finalize);
}

DexFuture *
editor_document_operation_await (EditorDocumentOperation *self)
{
  dex_return_error_if_fail (self != NULL);

  return dex_ref (DEX_FUTURE (self->promise));
}

static gboolean
editor_document_operation_load (EditorDocumentOperation  *self,
                                EditorDocument           *document,
                                GFile                    *file,
                                GFile                    *real_file,
                                GError                  **error)
{
  g_autoptr(GtkSourceFileLoader) loader = NULL;
  g_autoptr(EditorSourceBuffer) buffer = NULL;
  g_autoptr(EditorSettings) settings = NULL;
  g_autoptr(GtkSourceFile) source_file = NULL;
  g_autoptr(GDateTime) time_modified = NULL;
  g_autoptr(GFileInfo) info = NULL;
  const char *etag_value;
  goffset standard_size;

  g_assert (self != NULL);
  g_assert (EDITOR_IS_DOCUMENT (document));
  g_assert (G_IS_FILE (file));
  g_assert (!real_file || G_IS_FILE (real_file));

  buffer = editor_source_buffer_new ();
  settings = editor_document_dup_settings (document);
  source_file = gtk_source_file_new ();
  loader = gtk_source_file_loader_new (GTK_SOURCE_BUFFER (buffer), source_file);

  gtk_source_file_set_location (source_file, file);

  if (real_file != NULL)
    {
      if ((info = dex_await_object (dex_file_query_info (file,
                                                         G_FILE_ATTRIBUTE_TIME_MODIFIED","
                                                         G_FILE_ATTRIBUTE_STANDARD_SIZE","
                                                         G_FILE_ATTRIBUTE_ETAG_VALUE",",
                                                         G_FILE_QUERY_INFO_NONE,
                                                         G_PRIORITY_DEFAULT),
                                    error)))
        {
          standard_size = g_file_info_get_size (info);
          etag_value = g_file_info_get_etag (info);
          time_modified = g_file_info_get_modification_date_time (info);
        }

      dex_await (editor_settings_load (settings, real_file), NULL);
    }

  if (!dex_await (editor_source_file_loader_load (loader, G_PRIORITY_DEFAULT, self->progress), error))
    return FALSE;

  if (real_file != NULL)
    gtk_source_file_set_location (source_file, real_file);

  /* Apply cursor position */

  _editor_document_set_buffer (document, buffer, source_file);

  return TRUE;
}

static DexFuture *
editor_document_operation_open (EditorDocumentOperation *self,
                                EditorDocument          *document)
{
  g_autoptr(GtkSourceFileLoader) loader = NULL;
  g_autoptr(EditorSourceBuffer) buffer = NULL;
  g_autoptr(GtkSourceFile) source_file = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) draft_file = NULL;

  dex_return_error_if_fail (self != NULL);
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (document));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));
  dex_return_error_if_fail (G_IS_FILE (self->file));

  buffer = editor_source_buffer_new ();
  source_file = gtk_source_file_new ();
  draft_file = editor_draft_dup_file (self->draft);

  if (editor_document_operation_load (self, document, draft_file, self->file, NULL))
    return dex_future_new_true ();

  if (editor_document_operation_load (self, document, self->file, self->file, &error))
    return dex_future_new_true ();

  return dex_future_new_true ();
}

static DexFuture *
editor_document_operation_discard_changes (EditorDocumentOperation *self,
                                           EditorDocument          *document)
{
  dex_return_error_if_fail (self != NULL);
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (document));
  dex_return_error_if_fail (G_IS_FILE (self->file));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));

  dex_await (editor_draft_delete (self->draft), NULL);

  return editor_document_operation_open (self, document);
}

static DexFuture *
editor_document_operation_autosave (EditorDocumentOperation *self,
                                    EditorDocument          *document)
{
  g_autoptr(EditorSourceBuffer) buffer = NULL;
  g_autoptr(GtkSourceFileSaver) saver = NULL;
  g_autoptr(GtkSourceFile) source_file = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) file = NULL;

  dex_return_error_if_fail (self != NULL);
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (document));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));
  dex_return_error_if_fail (EDITOR_IS_SETTINGS (self->settings));

  file = editor_draft_dup_file (self->draft);
  buffer = editor_document_dup_buffer (document);

  source_file = gtk_source_file_new ();
  gtk_source_file_set_location (source_file, file);

  saver = gtk_source_file_saver_new (GTK_SOURCE_BUFFER (buffer), source_file);

  if (!dex_await (editor_source_file_saver_save (saver, G_PRIORITY_DEFAULT, self->progress), &error))
    {
    }

  if (!dex_await (editor_settings_save (self->settings, file), &error))
    {
    }

  return NULL;
}

static DexFuture *
editor_document_operation_save (EditorDocumentOperation *self,
                                EditorDocument          *document)
{
  dex_return_error_if_fail (self != NULL);
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (document));
  dex_return_error_if_fail (G_IS_FILE (self->file));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));

  return NULL;
}

static DexFuture *
editor_document_operation_save_as (EditorDocumentOperation *self,
                                   EditorDocument          *document)
{
  dex_return_error_if_fail (self != NULL);
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (document));
  dex_return_error_if_fail (G_IS_FILE (self->file));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));

  return NULL;
}

static DexFuture *
editor_document_operation_save_copy (EditorDocumentOperation *self,
                                     EditorDocument          *document)
{
  dex_return_error_if_fail (self != NULL);
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (document));
  dex_return_error_if_fail (G_IS_FILE (self->file));

  return NULL;
}

static DexFuture *
editor_document_operation_print (EditorDocumentOperation *self,
                                 EditorDocument          *document)
{
  return NULL;
}

void
editor_document_operation_run (EditorDocumentOperation *self,
                               EditorDocument          *document)
{
  g_autoptr(GError) error = NULL;
  DexFuture *future = NULL;

  g_return_if_fail (self != NULL);
  g_return_if_fail (EDITOR_IS_DOCUMENT (document));

  switch (self->kind)
    {
    case EDITOR_DOCUMENT_OPERATION_DISCARD_CHANGES:
      future = editor_document_operation_discard_changes (self, document);
      break;

    case EDITOR_DOCUMENT_OPERATION_OPEN:
      future = editor_document_operation_open (self, document);
      break;

    case EDITOR_DOCUMENT_OPERATION_AUTOSAVE:
      future = editor_document_operation_autosave (self, document);
      break;

    case EDITOR_DOCUMENT_OPERATION_SAVE:
      future = editor_document_operation_save (self, document);
      break;

    case EDITOR_DOCUMENT_OPERATION_SAVE_COPY:
      future = editor_document_operation_save_copy (self, document);
      break;

    case EDITOR_DOCUMENT_OPERATION_SAVE_AS:
      future = editor_document_operation_save_as (self, document);
      break;

    case EDITOR_DOCUMENT_OPERATION_PRINT:
      future = editor_document_operation_print (self, document);
      break;

    default:
      g_assert_not_reached ();
    }

  if (future == NULL)
    {
      dex_promise_resolve_boolean (self->promise, TRUE);
      return;
    }

  if (!dex_await (dex_ref (future), &error))
    dex_promise_reject (self->promise, g_steal_pointer (&error));
  else
    dex_promise_resolve (self->promise, dex_future_get_value (future, NULL));
}
