/* editor-document.c
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

#define G_LOG_DOMAIN "editor-document"

#include "config.h"

#include <string.h>

#include "editor-application.h"
#include "editor-document-private.h"
#include "editor-session-private.h"
#include "editor-window.h"

#define METATDATA_CURSOR "metadata::gnome-text-editor-cursor"
#define TITLE_MAX_LEN    20

struct _EditorDocument
{
  GtkSourceBuffer  parent_instance;

  GtkSourceFile   *file;
  gchar           *draft_id;

  guint            busy_count;
  gdouble          busy_progress;

  guint            readonly : 1;
};

typedef struct
{
  gchar *position;
  guint  line;
  guint  line_offset;
} Save;

typedef struct
{
  GFile           *file;
  GFile           *draft_file;
  gchar           *content_type;
  GMountOperation *mount_operation;
  gint64           draft_modified_at;
  gint64           modified_at;
  guint            n_active;
  guint            highlight_syntax : 1;
  guint            has_draft : 1;
  guint            has_file : 1;
} Load;

G_DEFINE_TYPE (EditorDocument, editor_document, GTK_SOURCE_TYPE_BUFFER)

enum {
  PROP_0,
  PROP_BUSY,
  PROP_BUSY_PROGRESS,
  PROP_FILE,
  N_PROPS
};

enum {
  FIRST_LINE_CHANGED,
  N_SIGNALS
};

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

static void
load_free (Load *load)
{
  g_clear_object (&load->file);
  g_clear_object (&load->draft_file);
  g_clear_object (&load->mount_operation);
  g_clear_pointer (&load->content_type, g_free);
  g_slice_free (Load, load);
}

static void
save_free (Save *save)
{
  g_clear_pointer (&save->position, g_free);
  g_slice_free (Save, save);
}

static GMountOperation *
editor_document_mount_operation_factory (GtkSourceFile *file,
                                         gpointer       user_data)
{
  GMountOperation *mount_operation = user_data;

  g_assert (GTK_SOURCE_IS_FILE (file));
  g_assert (!mount_operation || G_IS_MOUNT_OPERATION (mount_operation));

  return mount_operation ? g_object_ref (mount_operation) : NULL;
}

static void
editor_document_load_notify_completed_cb (EditorDocument *self,
                                          GParamSpec     *pspec,
                                          GTask          *task)
{
  EditorSession *session;

  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (G_IS_TASK (task));

  session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);
  _editor_session_document_seen (session, self);
}

static void
editor_document_save_notify_completed_cb (EditorDocument *self,
                                          GParamSpec     *pspec,
                                          GTask          *task)
{
  EditorSession *session;

  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (G_IS_TASK (task));

  session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);

  _editor_session_document_seen (session, self);

  /* If we saved successfully, we no longer need to track
   * this draft-id as it will be loaded as part of the recent
   * files. If future drafts are saved, it will be re-added
   * back to the session.
   */
  if (!g_task_had_error (task))
    _editor_session_remove_draft (session, self->draft_id);
}

static void
editor_document_insert_text (GtkTextBuffer *buffer,
                             GtkTextIter   *pos,
                             const gchar   *new_text,
                             gint           new_text_length)
{
  EditorDocument *self = (EditorDocument *)buffer;
  gboolean is_first;

  g_assert (GTK_IS_TEXT_BUFFER (buffer));
  g_assert (pos != NULL);
  g_assert (new_text != NULL);

  is_first = gtk_text_iter_get_line (pos) == 0;

  GTK_TEXT_BUFFER_CLASS (editor_document_parent_class)->insert_text (buffer, pos, new_text, new_text_length);

  if (is_first)
    g_signal_emit (self, signals[FIRST_LINE_CHANGED], 0);
}

static void
editor_document_delete_range (GtkTextBuffer *buffer,
                              GtkTextIter   *start,
                              GtkTextIter   *end)
{
  EditorDocument *self = (EditorDocument *)buffer;
  gboolean is_first;

  g_assert (GTK_IS_TEXT_BUFFER (buffer));
  g_assert (start != NULL);
  g_assert (end != NULL);

  /* gtktextbuffer.c orders start/end so only check start */
  is_first = gtk_text_iter_get_line (start) == 0;

  GTK_TEXT_BUFFER_CLASS (editor_document_parent_class)->delete_range (buffer, start, end);

  if (is_first)
    g_signal_emit (self, signals[FIRST_LINE_CHANGED], 0);
}

static GFile *
editor_document_get_draft_file (EditorDocument *self)
{
  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (self->draft_id != NULL);

  return g_file_new_build_filename (g_get_user_data_dir (),
                                    APP_ID,
                                    "drafts",
                                    self->draft_id,
                                    NULL);
}

static void
editor_document_progress (goffset  current_num_bytes,
                          goffset  total_num_bytes,
                          gpointer user_data)
{
  EditorDocument *self = user_data;

  g_assert (EDITOR_IS_DOCUMENT (self));

  if (total_num_bytes == 0 || current_num_bytes > total_num_bytes)
    self->busy_progress = 1.0;
  else
    self->busy_progress = (gdouble)current_num_bytes / (gdouble)total_num_bytes;

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_BUSY_PROGRESS]);
}

static void
editor_document_finalize (GObject *object)
{
  EditorDocument *self = (EditorDocument *)object;

  g_clear_object (&self->file);
  g_clear_pointer (&self->draft_id, g_free);

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
    case PROP_BUSY:
      g_value_set_boolean (value, editor_document_get_busy (self));
      break;

    case PROP_BUSY_PROGRESS:
      g_value_set_double (value, editor_document_get_busy_progress (self));
      break;

    case PROP_FILE:
      g_value_set_object (value, editor_document_get_file (self));
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
    case PROP_FILE:
      gtk_source_file_set_location (self->file, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_document_class_init (EditorDocumentClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkTextBufferClass *buffer_class = GTK_TEXT_BUFFER_CLASS (klass);

  object_class->finalize = editor_document_finalize;
  object_class->get_property = editor_document_get_property;
  object_class->set_property = editor_document_set_property;

  buffer_class->insert_text = editor_document_insert_text;
  buffer_class->delete_range = editor_document_delete_range;

  properties [PROP_BUSY] =
    g_param_spec_boolean ("busy",
                          "Busy",
                          "If the document is busy loading or saving",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_BUSY_PROGRESS] =
    g_param_spec_double ("busy-progress",
                         "Busy Progress",
                         "The progress of the current busy operation",
                         -G_MAXDOUBLE, G_MAXDOUBLE, 0.0,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILE] =
    g_param_spec_object ("file",
                         "File",
                         "The documents file on disk",
                         G_TYPE_FILE,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals [FIRST_LINE_CHANGED] =
    g_signal_new ("first-line-changed",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0, NULL, NULL, NULL,
                  G_TYPE_NONE, 0);
}

static void
editor_document_init (EditorDocument *self)
{
  self->file = gtk_source_file_new ();
  self->draft_id = g_uuid_string_random ();
}

EditorDocument *
_editor_document_new (GFile       *file,
                      const gchar *draft_id)
{
  EditorDocument *self;

  self = g_object_new (EDITOR_TYPE_DOCUMENT,
                       "file", file,
                       NULL);

  if (draft_id != NULL)
    {
      g_clear_pointer (&self->draft_id, g_free);
      self->draft_id = g_strdup (draft_id);
    }

  return g_steal_pointer (&self);
}

EditorDocument *
editor_document_new_draft (void)
{
  return g_object_new (EDITOR_TYPE_DOCUMENT, NULL);
}

EditorDocument *
editor_document_new_for_file (GFile *file)
{
  g_return_val_if_fail (G_IS_FILE (file), NULL);

  return g_object_new (EDITOR_TYPE_DOCUMENT,
                       "file", file,
                       NULL);
}

/**
 * editor_document_get_file:
 * @self: a #EditorDocument
 *
 * Gets the file for the document, if any.
 *
 * Returns: (nullable): a #GFile or %NULL
 */
GFile *
editor_document_get_file (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  return gtk_source_file_get_location (self->file);
}

const gchar *
_editor_document_get_draft_id (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  return self->draft_id;
}

void
_editor_document_set_draft_id (EditorDocument *self,
                               const gchar    *draft_id)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  if (g_strcmp0 (draft_id, self->draft_id) != 0)
    {
      g_free (self->draft_id);
      self->draft_id = g_strdup (draft_id);

      if (self->draft_id == NULL)
        self->draft_id = g_uuid_string_random ();
    }
}

static void
editor_document_save_draft_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  GtkSourceFileSaver *saver = (GtkSourceFileSaver *)object;
  g_autoptr(GTask) task = user_data;
  g_autoptr(GError) error = NULL;
  EditorDocument *self;

  g_assert (GTK_SOURCE_IS_FILE_SAVER (saver));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);

  if (!gtk_source_file_saver_save_finish (saver, result, &error))
    g_task_return_error (task, g_steal_pointer (&error));
  else
    g_task_return_boolean (task, TRUE);

  _editor_document_unmark_busy (self);
}

void
editor_document_save_draft_async (EditorDocument      *self,
                                  GCancellable        *cancellable,
                                  GAsyncReadyCallback  callback,
                                  gpointer             user_data)
{
  g_autoptr(GtkSourceFileSaver) saver = NULL;
  g_autoptr(GTask) task = NULL;
  g_autoptr(GFile) draft_file = NULL;
  g_autoptr(GFile) draft_dir = NULL;
  g_autofree gchar *title = NULL;
  g_autofree gchar *uri = NULL;
  EditorSession *session;

  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (self->draft_id != NULL);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, editor_document_save_draft_async);

  /* First tell the session to track this draft */
  session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);
  title = _editor_document_dup_title (self);
  uri = _editor_document_dup_uri (self);
  _editor_session_add_draft (session, self->draft_id, title, uri);

  if (!gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (self)))
    {
      /* Do nothing if we are already up to date */
      g_task_return_boolean (task, TRUE);
      return;
    }

  draft_file = editor_document_get_draft_file (self);
  saver = gtk_source_file_saver_new_with_target (GTK_SOURCE_BUFFER (self),
                                                 self->file,
                                                 draft_file);
  gtk_source_file_saver_set_flags (saver,
                                   (GTK_SOURCE_FILE_SAVER_FLAGS_IGNORE_INVALID_CHARS |
                                    GTK_SOURCE_FILE_SAVER_FLAGS_IGNORE_MODIFICATION_TIME));

  /* TODO: Probably want to make this async. We can just create an
   * async variant in editor-utils.c for this.
   */
  draft_dir = g_file_get_parent (draft_file);
  g_file_make_directory_with_parents (draft_dir, cancellable, NULL);

  _editor_document_mark_busy (self);

  gtk_source_file_saver_save_async (saver,
                                    G_PRIORITY_DEFAULT,
                                    cancellable,
                                    editor_document_progress,
                                    self,
                                    NULL,
                                    editor_document_save_draft_cb,
                                    g_steal_pointer (&task));
}

gboolean
editor_document_save_draft_finish (EditorDocument  *self,
                                   GAsyncResult    *result,
                                   GError         **error)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
editor_document_save_position_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  EditorDocument *self;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!g_file_set_attributes_finish (file, result, NULL, &error))
    g_warning ("Failed to save cursor position: %s", error->message);

  self = g_task_get_source_object (task);
  _editor_document_unmark_busy (self);

  g_task_return_boolean (task, TRUE);
}

static void
editor_document_save_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  GtkSourceFileSaver *saver = (GtkSourceFileSaver *)object;
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GFile) draft = NULL;
  g_autoptr(GTask) task = user_data;
  EditorDocument *self;
  GtkSourceFile *source_file;
  GFile *file;
  Save *save;

  g_assert (GTK_SOURCE_IS_FILE_SAVER (saver));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  save = g_task_get_task_data (task);
  source_file = gtk_source_file_saver_get_file (saver);
  file = gtk_source_file_get_location (source_file);

  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (GTK_SOURCE_IS_FILE (source_file));
  g_assert (G_IS_FILE (file));
  g_assert (save != NULL);

  if (!gtk_source_file_saver_save_finish (saver, result, &error))
    {
      _editor_document_unmark_busy (self);
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Delete the draft in case we had one */
  draft = editor_document_get_draft_file (self);
  g_file_delete_async (draft, G_PRIORITY_DEFAULT, NULL, NULL, NULL);

  info = g_file_info_new ();
  g_file_info_set_attribute_string (info, METATDATA_CURSOR, save->position);
  g_file_set_attributes_async (file,
                               info,
                               G_FILE_QUERY_INFO_NONE,
                               G_PRIORITY_DEFAULT,
                               g_task_get_cancellable (task),
                               editor_document_save_position_cb,
                               g_object_ref (task));
}

void
editor_document_save_async (EditorDocument      *self,
                            GFile               *file,
                            GCancellable        *cancellable,
                            GAsyncReadyCallback  callback,
                            gpointer             user_data)
{
  g_autoptr(GtkSourceFileSaver) saver = NULL;
  g_autoptr(GTask) task = NULL;
  g_autoptr(GFile) draft_file = NULL;
  GtkTextMark *insert;
  GtkTextIter iter;
  Save *save;

  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (self->draft_id != NULL);

  insert = gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (self));
  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (self), &iter, insert);

  save = g_slice_new0 (Save);
  save->line = gtk_text_iter_get_line (&iter);
  save->line_offset = gtk_text_iter_get_line_offset (&iter);
  save->position = g_strdup_printf ("%u:%u", save->line, save->line_offset);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, editor_document_save_draft_async);
  g_task_set_task_data (task, save, (GDestroyNotify)save_free);

  g_signal_connect_object (task,
                           "notify::completed",
                           G_CALLBACK (editor_document_save_notify_completed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  if (editor_document_get_busy (self))
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_BUSY,
                               "Cannot save document while it is busy");
      return;
    }

  if (file == NULL)
    {
      file = editor_document_get_file (self);

      if (file == NULL)
        {
          g_task_return_new_error (task,
                                   G_IO_ERROR,
                                   G_IO_ERROR_INVALID_FILENAME,
                                   "Cannot save document without a file");
          return;
        }
    }

  g_assert (G_IS_FILE (file));

  if (editor_document_get_file (self) == NULL)
    {
      gtk_source_file_set_location (self->file, file);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_FILE]);
    }

  saver = gtk_source_file_saver_new_with_target (GTK_SOURCE_BUFFER (self),
                                                 self->file,
                                                 file);
  gtk_source_file_saver_set_flags (saver,
                                   (GTK_SOURCE_FILE_SAVER_FLAGS_IGNORE_INVALID_CHARS |
                                    GTK_SOURCE_FILE_SAVER_FLAGS_IGNORE_MODIFICATION_TIME));

  _editor_document_mark_busy (self);

  gtk_source_file_saver_save_async (saver,
                                    G_PRIORITY_DEFAULT,
                                    NULL,
                                    editor_document_progress,
                                    self ,
                                    NULL,
                                    editor_document_save_cb,
                                    g_steal_pointer (&task));
}

gboolean
editor_document_save_finish (EditorDocument  *self,
                             GAsyncResult    *result,
                             GError         **error)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

void
_editor_document_mark_busy (EditorDocument *self)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  self->busy_count++;

  if (self->busy_count == 1)
    {
      self->busy_progress = 0;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUSY]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUSY_PROGRESS]);
    }
}

void
_editor_document_unmark_busy (EditorDocument *self)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (self->busy_count > 0);

  self->busy_count--;

  if (self->busy_count == 0)
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUSY]);
}

gboolean
editor_document_get_busy (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);

  return self->busy_count > 0;
}

static void
editor_document_set_readonly (EditorDocument *self,
                              gboolean        readonly)
{
  g_assert (EDITOR_IS_DOCUMENT (self));

  self->readonly = !!readonly;
}

static void
editor_document_query_info_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  g_autofree gchar *filename = NULL;
  GtkSourceLanguageManager *lm;
  GtkSourceLanguage *language;
  EditorDocument *self;
  const gchar *content_type;
  const gchar *position;
  gboolean readonly;
  gboolean is_modified;
  guint line = 0;
  guint line_offset = 0;
  Load *load;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  lm = gtk_source_language_manager_get_default ();
  self = g_task_get_source_object (task);
  filename = g_file_get_basename (file);

  g_assert (GTK_SOURCE_IS_LANGUAGE_MANAGER (lm));
  g_assert (EDITOR_IS_DOCUMENT (self));

  if (!(info = g_file_query_info_finish (file, result, &error)))
    {
      _editor_document_unmark_busy (self);
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  readonly = g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_FILESYSTEM_READONLY);
  content_type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
  position = g_file_info_get_attribute_string (info, METATDATA_CURSOR);

  editor_document_set_readonly (self, readonly);

  language = gtk_source_language_manager_guess_language (lm, filename, content_type);
  gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (self), language);
  gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (self), language != NULL);

  /* Parse metadata for cursor position */
  if (position != NULL &&
      sscanf (position, "%u:%u", &line, &line_offset) >= 1)
    {
      GtkTextIter iter;

      g_debug ("Restoring insert mark to %u:%u", line + 1, line_offset + 1);
      gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (self),
                                               &iter,
                                               line,
                                               line_offset);
      gtk_text_buffer_select_range (GTK_TEXT_BUFFER (self), &iter, &iter);
    }

  load = g_task_get_task_data (task);

  is_modified = load->has_draft;

  if (load->has_file == FALSE)
    {
      GtkTextIter iter;

      gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (self), &iter);
      if (!gtk_text_iter_is_start (&iter))
        is_modified = TRUE;
    }

  gtk_text_buffer_set_modified (GTK_TEXT_BUFFER (self), is_modified);

  if (load->highlight_syntax)
    gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (self), TRUE);

  _editor_document_unmark_busy (self);

  g_task_return_boolean (task, TRUE);
}

static void
editor_document_load_cb (GObject      *object,
                         GAsyncResult *result,
                         gpointer      user_data)
{
  GtkSourceFileLoader *loader = (GtkSourceFileLoader *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  EditorDocument *self;

  g_assert (GTK_SOURCE_IS_FILE_LOADER (loader));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);

  if (!gtk_source_file_loader_load_finish (loader, result, &error))
    {
      g_warning ("Failed to load file: %s", error->message);
      g_task_return_error (task, g_steal_pointer (&error));
      _editor_document_unmark_busy (self);
      return;
    }
  else
    {
      GFile *file = editor_document_get_file (self);
      g_autoptr(GFile) draft_file = NULL;
      GtkTextIter begin;

      g_assert (!file || G_IS_FILE (file));

      gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (self), &begin);
      gtk_text_buffer_select_range (GTK_TEXT_BUFFER (self), &begin, &begin);

      if (file == NULL)
        file = draft_file = editor_document_get_draft_file (self);

      g_file_query_info_async (file,
                               G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE","
                               G_FILE_ATTRIBUTE_FILESYSTEM_READONLY","
                               METATDATA_CURSOR,
                               G_FILE_QUERY_INFO_NONE,
                               G_PRIORITY_DEFAULT,
                               g_task_get_cancellable (task),
                               editor_document_query_info_cb,
                               g_object_ref (task));
    }
}

static void
editor_document_do_load (EditorDocument *self,
                         GTask          *task,
                         Load           *load)
{
  g_autoptr(GtkSourceFileLoader) loader = NULL;
  g_autoptr(GtkSourceFile) file = NULL;

  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (G_IS_TASK (task));

  if (load->has_draft == FALSE && load->has_file == FALSE)
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_INVALID_FILENAME,
                               "Failed to locate file or draft");
      return;
    }

  file = gtk_source_file_new ();

  if (load->mount_operation != NULL)
    gtk_source_file_set_mount_operation_factory (file,
                                                 editor_document_mount_operation_factory,
                                                 g_object_ref (load->mount_operation),
                                                 g_object_unref);

  if (load->has_draft)
    gtk_source_file_set_location (file, load->draft_file);
  else
    gtk_source_file_set_location (file, load->file);

  loader = gtk_source_file_loader_new (GTK_SOURCE_BUFFER (self), file);

  gtk_source_file_loader_load_async (loader,
                                     G_PRIORITY_DEFAULT,
                                     g_task_get_cancellable (task),
                                     editor_document_progress,
                                     self,
                                     NULL,
                                     editor_document_load_cb,
                                     g_object_ref (task));
}

static void
editor_document_load_draft_info_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  EditorDocument *self;
  Load *load;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  load = g_task_get_task_data (task);

  if ((info = g_file_query_info_finish (file, result, &error)))
    {
      load->modified_at = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
      load->has_draft = TRUE;
    }

  load->n_active--;

  if (load->n_active == 0)
    editor_document_do_load (self, task, load);
}

static void
editor_document_load_file_info_cb (GObject      *object,
                                   GAsyncResult *result,
                                   gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  EditorDocument *self;
  Load *load;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  load = g_task_get_task_data (task);

  if (!(info = g_file_query_info_finish (file, result, &error)))
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED))
        {
          load->has_file = TRUE;
          load->content_type = NULL;
          load->modified_at = 0;
        }
      else
        {
          g_warning ("Failed to query information about file: %s",
                     error->message);
        }
    }
  else
    {
      load->has_file = TRUE;
      load->content_type = g_strdup (g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE));
      load->modified_at = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
    }

  load->n_active--;

  if (load->n_active == 0)
    editor_document_do_load (self, task, load);
}

static void
editor_document_load_file_mount_cb (GObject      *object,
                                    GAsyncResult *result,
                                    gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  Load *load;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!g_file_mount_enclosing_volume_finish (file, result, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED) &&
          !g_error_matches (error, G_IO_ERROR, G_IO_ERROR_ALREADY_MOUNTED))
        {
          /* Don't decrement n_active, since we don't want to
           * race for completion of the task.
           */
          g_warning ("Failed to mount enclosing volume: %s", error->message);
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }
    }

  load = g_task_get_task_data (task);

  g_assert (load != NULL);
  g_assert (G_IS_FILE (load->file));
  g_assert (load->n_active > 0);

  g_file_query_info_async (load->file,
                           G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE","
                           G_FILE_ATTRIBUTE_ACCESS_CAN_READ","
                           G_FILE_ATTRIBUTE_STANDARD_SIZE","
                           G_FILE_ATTRIBUTE_TIME_MODIFIED,
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           g_task_get_cancellable (task),
                           editor_document_load_file_info_cb,
                           g_object_ref (task));
}

void
_editor_document_load_async (EditorDocument      *self,
                             EditorWindow        *window,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  GFile *file;
  Load *load;

  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (!window || EDITOR_IS_WINDOW (window));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  file = editor_document_get_file (self);

  load = g_slice_new0 (Load);
  load->file = file ? g_file_dup (file) : NULL;
  load->draft_file = editor_document_get_draft_file (self);

  if (window != NULL)
    load->mount_operation = gtk_mount_operation_new (GTK_WINDOW (window));
  else
    load->mount_operation = g_mount_operation_new ();

  if (gtk_source_buffer_get_highlight_syntax (GTK_SOURCE_BUFFER (self)))
    load->highlight_syntax = TRUE;

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, _editor_document_load_async);
  g_task_set_task_data (task, load, (GDestroyNotify) load_free);

  g_signal_connect_object (task,
                           "notify::completed",
                           G_CALLBACK (editor_document_load_notify_completed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  _editor_document_mark_busy (self);

  /* Disable features while the file is loading to speed things up
   * and reduce the chances that syntax highlights can hit scenarios
   * where text changes during the main loop idle callbacks.
   */
  gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (self), FALSE);

  load->n_active++;
  g_file_query_info_async (load->draft_file,
                           G_FILE_ATTRIBUTE_ACCESS_CAN_READ","
                           G_FILE_ATTRIBUTE_STANDARD_SIZE","
                           G_FILE_ATTRIBUTE_TIME_MODIFIED,
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           cancellable,
                           editor_document_load_draft_info_cb,
                           g_object_ref (task));

  if (load->file != NULL)
    {
      load->n_active++;
      /* First make sure we have the enclosing mount available
       * before we start querying things.
       */
      g_file_mount_enclosing_volume (load->file,
                                     G_MOUNT_MOUNT_NONE,
                                     load->mount_operation,
                                     cancellable,
                                     editor_document_load_file_mount_cb,
                                     g_object_ref (task));
    }
}

gboolean
_editor_document_load_finish (EditorDocument  *self,
                              GAsyncResult    *result,
                              GError         **error)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
editor_document_guess_language_query_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  g_autofree gchar *filename = NULL;
  GtkSourceLanguageManager *lm;
  GtkSourceLanguage *language;
  EditorDocument *self;
  const gchar *content_type;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (result));

  if (!(info = g_file_query_info_finish (file, result, &error)))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  content_type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
  filename = g_file_get_basename (file);
  lm = gtk_source_language_manager_get_default ();
  language = gtk_source_language_manager_guess_language (lm, filename, content_type);
  self = g_task_get_source_object (task);

  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (GTK_SOURCE_IS_LANGUAGE_MANAGER (lm));
  g_assert (!language || GTK_SOURCE_IS_LANGUAGE (language));

  if (language != NULL)
    gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (self), language);

  g_task_return_boolean (task, TRUE);
}

/**
 * editor_document_guess_language_async:
 * @self: a #EditorDocument
 * @cancellable: (nullable): a #GCancellable
 * @callback: a #GAsyncReadyCallback
 * @user_data: closure data for @callback
 *
 * Asynchronously guesses the language for @self using the current filename
 * and content-type of the file.
 */
void
editor_document_guess_language_async (EditorDocument      *self,
                                      GCancellable        *cancellable,
                                      GAsyncReadyCallback  callback,
                                      gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  GFile *file;

  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, editor_document_guess_language_async);

  if (!(file = editor_document_get_file (self)))
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_INVALID_FILENAME,
                             "File has not been saved, cannot guess content-type");
  else
    g_file_query_info_async (file,
                             G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                             G_FILE_QUERY_INFO_NONE,
                             G_PRIORITY_DEFAULT,
                             cancellable,
                             editor_document_guess_language_query_cb,
                             g_steal_pointer (&task));
}

/**
 * editor_document_guess_language_finish:
 * @self: a #EditorDocument
 * @result: the #GAsyncResult provided to callback
 * @error: a location for a #GError, or %NULL
 *
 * Returns: %TRUE if successful; otherwise %FALSE and @error is set
 */
gboolean
editor_document_guess_language_finish (EditorDocument  *self,
                                       GAsyncResult    *result,
                                       GError         **error)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (self), error);
}

/**
 * editor_document_get_busy_progress:
 * @self: a #EditorDocument
 *
 * Gets the progress of the current busy operation.
 *
 * Returns: A fraction between 0.0 and 1.0.
 */
gdouble
editor_document_get_busy_progress (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), 0.0);

  return self->busy_progress;
}

GFile *
_editor_document_get_draft_file (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  return g_file_new_build_filename (g_get_user_data_dir (),
                                    APP_ID,
                                    "drafts",
                                    self->draft_id,
                                    NULL);
}

gchar *
_editor_document_dup_title (EditorDocument *self)
{
  g_autofree gchar *slice = NULL;
  GFile *file;
  GtkTextIter begin;
  GtkTextIter end;
  guint i;

  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  file = editor_document_get_file (self);

  if (file != NULL)
    return g_file_get_basename (file);

  gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (self), &begin);

  for (end = begin, i = 0; i < TITLE_MAX_LEN; i++)
    {
      if (gtk_text_iter_ends_line (&end))
        break;

      if (!gtk_text_iter_forward_char (&end))
        break;
    }

  slice = g_strstrip (gtk_text_iter_get_slice (&begin, &end));

  if (slice[0] == '\0')
    return NULL;

  return g_steal_pointer (&slice);
}

gchar *
_editor_document_dup_uri (EditorDocument *self)
{
  GFile *file;

  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  if ((file = editor_document_get_file (self)))
    return g_file_get_uri (file);

  return NULL;
}
