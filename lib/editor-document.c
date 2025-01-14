/*
 * editor-document.c
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

#include "editor-document-private.h"
#include "editor-document-operation.h"
#include "editor-draft.h"
#include "editor-progress.h"
#include "editor-settings.h"
#include "editor-source-buffer.h"

struct _EditorDocument
{
  GObject              parent_instance;

  EditorSourceBuffer  *buffer;
  EditorDraft         *draft;
  GFile               *file;
  EditorSettings      *settings;
  GtkSourceFile       *source_file;

  DexFuture           *worker;
  DexChannel          *operations;
};

enum {
  PROP_0,
  PROP_BUFFER,
  PROP_DRAFT,
  PROP_FILE,
  PROP_SETTINGS,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorDocument, editor_document, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

typedef struct _Worker
{
  GWeakRef    document_wr;
  DexChannel *channel;
} Worker;

static void
worker_free (Worker *worker)
{
  g_weak_ref_clear (&worker->document_wr);
  dex_clear (&worker->channel);
  g_free (worker);
}

static DexFuture *
editor_document_worker (gpointer data)
{
  Worker *state = data;

  g_assert (state != NULL);
  g_assert (DEX_IS_CHANNEL (state->channel));

  for (;;)
    {
      g_autoptr(EditorDocumentOperation) operation = NULL;
      g_autoptr(EditorDocument) document = NULL;

      if (!(operation = dex_await_boxed (dex_channel_receive (state->channel), NULL)))
        break;

      if (!(document = g_weak_ref_get (&state->document_wr)))
        break;

      editor_document_operation_run (operation, document);
    }

  return NULL;
}

static void
editor_document_constructed (GObject *object)
{
  EditorDocument *self = (EditorDocument *)object;
  Worker *state;

  G_OBJECT_CLASS (editor_document_parent_class)->constructed (object);

  state = g_new0 (Worker, 1);
  g_weak_ref_init (&state->document_wr, self);
  state->channel = dex_ref (state->channel);

  self->worker = dex_scheduler_spawn (NULL, 0,
                                      editor_document_worker,
                                      state,
                                      (GDestroyNotify) worker_free);
}

static void
editor_document_dispose (GObject *object)
{
  EditorDocument *self = (EditorDocument *)object;

  dex_channel_close_send (self->operations);

  G_OBJECT_CLASS (editor_document_parent_class)->dispose (object);
}

static void
editor_document_finalize (GObject *object)
{
  EditorDocument *self = (EditorDocument *)object;

  g_clear_object (&self->buffer);
  g_clear_object (&self->draft);
  g_clear_object (&self->file);
  g_clear_object (&self->settings);
  g_clear_object (&self->source_file);

  dex_clear (&self->operations);
  dex_clear (&self->worker);

  G_OBJECT_CLASS (editor_document_parent_class)->finalize (object);
}

static void
editor_document_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EditorDocument *self = EDITOR_DOCUMENT (object);

  switch (prop_id)
    {
    case PROP_BUFFER:
      g_value_take_object (value, editor_document_dup_buffer (self));
      break;

    case PROP_DRAFT:
      g_value_take_object (value, editor_document_dup_draft (self));
      break;

    case PROP_FILE:
      g_value_take_object (value, editor_document_dup_file (self));
      break;

    case PROP_SETTINGS:
      g_value_take_object (value, editor_document_dup_settings (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_document_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  EditorDocument *self = EDITOR_DOCUMENT (object);

  switch (prop_id)
    {
    case PROP_DRAFT:
      self->draft = g_value_dup_object (value);
      break;

    case PROP_FILE:
      self->file = g_value_dup_object (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_document_class_init (EditorDocumentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = editor_document_constructed;
  object_class->dispose = editor_document_dispose;
  object_class->finalize = editor_document_finalize;
  object_class->get_property = editor_document_get_property;
  object_class->set_property = editor_document_set_property;

  properties[PROP_BUFFER] =
    g_param_spec_object ("buffer", NULL, NULL,
                         EDITOR_TYPE_SOURCE_BUFFER,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_DRAFT] =
    g_param_spec_object ("draft", NULL, NULL,
                         EDITOR_TYPE_DRAFT,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_FILE] =
    g_param_spec_object ("file", NULL, NULL,
                         G_TYPE_FILE,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_SETTINGS] =
    g_param_spec_object ("settings", NULL, NULL,
                         EDITOR_TYPE_SETTINGS,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_document_init (EditorDocument *self)
{
  self->buffer = editor_source_buffer_new ();
  self->settings = editor_settings_new (self);
  self->operations = dex_channel_new (0);
}

gboolean
editor_document_can_discard (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);

  if (self->file == NULL || self->draft == NULL)
    return FALSE;

  if (!gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (self->buffer)))
    return FALSE;

  return TRUE;
}

static DexFuture *
editor_document_operation (EditorDocument              *self,
                           EditorDocumentOperationKind  kind,
                           GFile                       *file,
                           EditorProgress              *progress)
{
  g_autoptr(EditorDocumentOperation) oper = NULL;
  g_autoptr(DexFuture) future = NULL;
  g_autoptr(DexFuture) wrapped = NULL;

  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (!file || G_IS_FILE (file));
  g_assert (!progress || EDITOR_IS_PROGRESS (progress));

  oper = editor_document_operation_new (kind,
                                        file ? file : self->file,
                                        self->draft,
                                        self->settings,
                                        progress);
  future = editor_document_operation_await (oper);
  wrapped = dex_future_new_take_boxed (EDITOR_TYPE_DOCUMENT_OPERATION,
                                       g_steal_pointer (&oper));
  dex_future_disown (dex_channel_send (self->operations, g_steal_pointer (&wrapped)));
  return g_steal_pointer (&future);
}

DexFuture *
editor_document_discard_changes (EditorDocument *self,
                                 EditorProgress *progress)
{
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (self));
  dex_return_error_if_fail (!progress || EDITOR_IS_PROGRESS (progress));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));
  dex_return_error_if_fail (!self->file || G_IS_FILE (self->file));

  return editor_document_operation (self,
                                    EDITOR_DOCUMENT_OPERATION_DISCARD_CHANGES,
                                    self->file,
                                    progress);
}

DexFuture *
editor_document_open (EditorDocument *self,
                      EditorProgress *progress)
{
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (self));
  dex_return_error_if_fail (!progress || EDITOR_IS_PROGRESS (progress));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));
  dex_return_error_if_fail (!self->file || G_IS_FILE (self->file));

  return editor_document_operation (self,
                                    EDITOR_DOCUMENT_OPERATION_OPEN,
                                    self->file,
                                    progress);
}

DexFuture *
editor_document_autosave (EditorDocument *self,
                          EditorProgress *progress)
{
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (self));
  dex_return_error_if_fail (!progress || EDITOR_IS_PROGRESS (progress));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));

  return editor_document_operation (self,
                                    EDITOR_DOCUMENT_OPERATION_AUTOSAVE,
                                    self->file,
                                    progress);
}

DexFuture *
editor_document_save (EditorDocument *self,
                      EditorProgress *progress)
{
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (self));
  dex_return_error_if_fail (!progress || EDITOR_IS_PROGRESS (progress));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));
  dex_return_error_if_fail (G_IS_FILE (self->file));

  return editor_document_operation (self,
                                    EDITOR_DOCUMENT_OPERATION_SAVE,
                                    self->file,
                                    progress);
}

DexFuture *
editor_document_save_copy (EditorDocument *self,
                           GFile          *file,
                           EditorProgress *progress)
{
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (self));
  dex_return_error_if_fail (!progress || EDITOR_IS_PROGRESS (progress));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));
  dex_return_error_if_fail (G_IS_FILE (self->file));

  return editor_document_operation (self,
                                    EDITOR_DOCUMENT_OPERATION_SAVE_COPY,
                                    file,
                                    progress);
}

DexFuture *
editor_document_save_as (EditorDocument *self,
                         GFile          *file,
                         EditorProgress *progress)
{
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (self));
  dex_return_error_if_fail (!progress || EDITOR_IS_PROGRESS (progress));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));
  dex_return_error_if_fail (G_IS_FILE (self->file));

  return editor_document_operation (self,
                                    EDITOR_DOCUMENT_OPERATION_SAVE_AS,
                                    file,
                                    progress);
}

DexFuture *
editor_document_print (EditorDocument *self,
                       EditorProgress *progress)
{
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (self));
  dex_return_error_if_fail (!progress || EDITOR_IS_PROGRESS (progress));
  dex_return_error_if_fail (EDITOR_IS_DRAFT (self->draft));
  dex_return_error_if_fail (!self->file || G_IS_FILE (self->file));

  return editor_document_operation (self,
                                    EDITOR_DOCUMENT_OPERATION_PRINT,
                                    NULL,
                                    progress);
}

void
_editor_document_set_buffer (EditorDocument     *self,
                             EditorSourceBuffer *buffer,
                             GtkSourceFile      *source_file)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (EDITOR_IS_SOURCE_BUFFER (buffer));
  g_return_if_fail (GTK_SOURCE_IS_FILE (source_file));

  g_set_object (&self->source_file, source_file);

  if (g_set_object (&self->buffer, buffer))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUFFER]);
}
