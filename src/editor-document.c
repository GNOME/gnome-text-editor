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

#include <glib/gi18n.h>
#include <string.h>

#include "editor-application.h"
#include "editor-buffer-monitor-private.h"
#include "editor-document-private.h"
#include "editor-spell-checker.h"
#include "editor-text-buffer-spell-adapter.h"
#include "editor-session-private.h"
#include "editor-window.h"

#define METATDATA_CURSOR    "metadata::gnome-text-editor-cursor"
#define TITLE_LAST_WORD_POS 20
#define TITLE_MAX_LEN       100

struct _EditorDocument
{
  GtkSourceBuffer               parent_instance;

  EditorBufferMonitor          *monitor;
  GtkSourceFile                *file;
  gchar                        *draft_id;
  GtkTextTag                   *line_spacing_tag;
  const GtkSourceEncoding      *encoding;

  EditorSpellChecker           *spell_checker;
  EditorTextBufferSpellAdapter *spell_adapter;

  GtkSourceNewlineType          newline_type;
  guint                         busy_count;
  gdouble                       busy_progress;

  guint                         readonly : 1;
  guint                         needs_autosave : 1;
  guint                         was_restored : 1;
  guint                         externally_modified : 1;
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
  PROP_EXTERNALLY_MODIFIED,
  PROP_FILE,
  PROP_SPELL_CHECKER,
  PROP_TITLE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];
static GSettings *shared_settings;

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

static void
editor_document_apply_spacing (EditorDocument *self)
{
  GtkTextIter begin, end;

  g_assert (EDITOR_IS_DOCUMENT (self));

  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (self), &begin, &end);
  gtk_text_buffer_apply_tag (GTK_TEXT_BUFFER (self), self->line_spacing_tag, &begin, &end);
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
editor_document_changed (GtkTextBuffer *buffer)
{
  EditorDocument *self = (EditorDocument *)buffer;

  g_assert (EDITOR_IS_DOCUMENT (self));

  /* Track separately from :modified for drafts */
  self->needs_autosave = TRUE;

  GTK_TEXT_BUFFER_CLASS (editor_document_parent_class)->changed (buffer);
}

static void
editor_document_guess_content_type (EditorDocument *self)
{
  GtkSourceLanguageManager *manager;
  GtkSourceLanguage *language;
  g_autofree char *content = NULL;
  g_autofree char *content_type = NULL;
  g_autofree char *filename = NULL;
  GtkTextIter begin, end;
  GFile *file;
  gboolean uncertain;

  g_assert (EDITOR_IS_DOCUMENT (self));

  /* Ignore if we already have a language */
  if ((language = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (self))))
    return;

  /* Read the first page of data and use it to guess content-type */
  gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (self), &begin);
  gtk_text_buffer_get_iter_at_offset (GTK_TEXT_BUFFER (self), &end, 4095);
  content = gtk_text_iter_get_slice (&begin, &end);

  if ((file = editor_document_get_file (self)))
    filename = g_file_get_basename (file);

  content_type = g_content_type_guess (filename, (const guchar *)content, strlen (content), &uncertain);
  manager = gtk_source_language_manager_get_default ();
  language = gtk_source_language_manager_guess_language (manager, filename, content_type);

  if (language)
    gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (self), language);
}

static void
editor_document_insert_text (GtkTextBuffer *buffer,
                             GtkTextIter   *pos,
                             const gchar   *new_text,
                             gint           new_text_length)
{
  EditorDocument *self = (EditorDocument *)buffer;
  guint line;
  guint offset;
  guint length;

  g_assert (GTK_IS_TEXT_BUFFER (buffer));
  g_assert (pos != NULL);
  g_assert (new_text != NULL);

  line = gtk_text_iter_get_line (pos);
  offset = gtk_text_iter_get_offset (pos);

  GTK_TEXT_BUFFER_CLASS (editor_document_parent_class)->insert_text (buffer, pos, new_text, new_text_length);

  length = gtk_text_iter_get_offset (pos) - offset;

  if (length > 0)
    editor_text_buffer_spell_adapter_insert_text (self->spell_adapter, offset, length);

  if (offset < TITLE_MAX_LEN && editor_document_get_file (self) == NULL)
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);

  if (self->busy_count == 0 && line == 0 && strchr (new_text, '\n'))
    editor_document_guess_content_type (self);
}

static void
editor_document_delete_range (GtkTextBuffer *buffer,
                              GtkTextIter   *start,
                              GtkTextIter   *end)
{
  EditorDocument *self = (EditorDocument *)buffer;
  guint offset;
  guint length;

  g_assert (GTK_IS_TEXT_BUFFER (buffer));
  g_assert (start != NULL);
  g_assert (end != NULL);
  g_assert (gtk_text_iter_compare (start, end) <= 0);

  offset = gtk_text_iter_get_offset (start);
  length = gtk_text_iter_get_offset (end) - offset;

  GTK_TEXT_BUFFER_CLASS (editor_document_parent_class)->delete_range (buffer, start, end);

  if (length > 0)
    editor_text_buffer_spell_adapter_delete_range (self->spell_adapter, offset, length);

  if (offset < TITLE_MAX_LEN && editor_document_get_file (self) == NULL)
    g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_TITLE]);
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
editor_document_set_busy_progress (EditorDocument *self,
                                   guint           stage,
                                   guint           n_stages,
                                   gdouble         value)
{
  gdouble per_stage;

  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (n_stages > 0);

  value = CLAMP (value, 0.0, 1.0);
  per_stage = 1.0 / n_stages;

  value = (per_stage * stage) + (value * per_stage);

  if (value != self->busy_progress)
    {
      self->busy_progress = value;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_BUSY_PROGRESS]);
    }
}

static void
editor_document_progress (goffset  current_num_bytes,
                          goffset  total_num_bytes,
                          gpointer user_data)
{
  EditorDocument *self = user_data;
  gdouble value;

  g_assert (EDITOR_IS_DOCUMENT (self));

  if (total_num_bytes == 0 || current_num_bytes > total_num_bytes)
    value = 1.0;
  else
    value = (gdouble)current_num_bytes / (gdouble)total_num_bytes;

  editor_document_set_busy_progress (self, 1, 2, value);
}

static void
editor_document_monitor_notify_changed_cb (EditorDocument      *self,
                                           GParamSpec          *pspec,
                                           EditorBufferMonitor *monitor)
{
  gboolean changed;

  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (EDITOR_IS_BUFFER_MONITOR (monitor));

  changed = editor_buffer_monitor_get_changed (monitor);
  _editor_document_set_externally_modified (self, changed);
}

static void
on_cursor_moved_cb (EditorDocument *self)
{
  GtkTextMark *mark;
  GtkTextIter iter;

  mark = gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (self));
  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (self), &iter, mark);

  editor_text_buffer_spell_adapter_cursor_moved (self->spell_adapter,
                                                 gtk_text_iter_get_offset (&iter));

}

static void
editor_document_constructed (GObject *object)
{
  EditorDocument *self = (EditorDocument *)object;

  if (shared_settings == NULL)
    shared_settings = g_settings_new ("org.gnome.TextEditor");

  G_OBJECT_CLASS (editor_document_parent_class)->constructed (object);

  self->spell_adapter = editor_text_buffer_spell_adapter_new (GTK_TEXT_BUFFER (self),
                                                              self->spell_checker);
  g_settings_bind (shared_settings, "spellcheck",
                   self->spell_adapter, "enabled",
                   G_SETTINGS_BIND_GET);

  self->line_spacing_tag = gtk_text_buffer_create_tag (GTK_TEXT_BUFFER (self),
                                                       NULL,
                                                       "pixels-below-lines", 2,
                                                       NULL);
  editor_document_apply_spacing (self);
}

static void
editor_document_finalize (GObject *object)
{
  EditorDocument *self = (EditorDocument *)object;

  g_clear_object (&self->monitor);
  g_clear_object (&self->file);
  g_clear_object (&self->spell_checker);
  g_clear_object (&self->spell_adapter);
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

    case PROP_EXTERNALLY_MODIFIED:
      g_value_set_boolean (value, editor_document_get_externally_modified (self));
      break;

    case PROP_FILE:
      g_value_set_object (value, editor_document_get_file (self));
      break;

    case PROP_TITLE:
      g_value_take_string (value, editor_document_dup_title (self));
      break;

    case PROP_SPELL_CHECKER:
      g_value_set_object (value, editor_document_get_spell_checker (self));
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

    case PROP_SPELL_CHECKER:
      editor_document_set_spell_checker (self, g_value_get_object (value));
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

  object_class->constructed = editor_document_constructed;
  object_class->finalize = editor_document_finalize;
  object_class->get_property = editor_document_get_property;
  object_class->set_property = editor_document_set_property;

  buffer_class->changed = editor_document_changed;
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

  properties [PROP_EXTERNALLY_MODIFIED] =
    g_param_spec_boolean ("externally-modified",
                          "Externally Modified",
                          "Externally Modified",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_FILE] =
    g_param_spec_object ("file",
                         "File",
                         "The documents file on disk",
                         G_TYPE_FILE,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SPELL_CHECKER] =
    g_param_spec_object ("spell-checker",
                         "Spell Checker",
                         "Spell Checker",
                         EDITOR_TYPE_SPELL_CHECKER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title for the document",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_document_init (EditorDocument *self)
{
  self->newline_type = GTK_SOURCE_NEWLINE_TYPE_DEFAULT;
  self->file = gtk_source_file_new ();
  self->draft_id = g_uuid_string_random ();
  self->spell_checker = editor_spell_checker_new (NULL, NULL);

  self->monitor = editor_buffer_monitor_new ();
  g_signal_connect_object (self->monitor,
                           "notify::changed",
                           G_CALLBACK (editor_document_monitor_notify_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_object_bind_property (self->file, "location",
                          self->monitor, "file",
                          G_BINDING_SYNC_CREATE);

  g_signal_connect (self, "cursor-moved", G_CALLBACK (on_cursor_moved_cb), NULL);
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

static gboolean
file_saver_save_finish (GtkSourceFileSaver  *saver,
                        GAsyncResult        *result,
                        GError             **error)
{
  g_assert (GTK_SOURCE_IS_FILE_SAVER (saver));
  g_assert (G_IS_ASYNC_RESULT (result));

  /*
   * This method exists to work around GtkSourceFileSaver when used for
   * saving to alternate files such as a draft. If we call
   * gtk_source_file_saver_save_finish() it will set the externally
   * modified flag and mess up our undo stack position.
   *
   * We don't care about any of those settings being propagated to the
   * GSourceFile either, so we can just check for the error manually from
   * the GTask instead of calling the GtkSourceView API.
   */

  if (G_IS_TASK (result))
    return g_task_propagate_boolean (G_TASK (result), error);
  else
    return gtk_source_file_saver_save_finish (saver, result, error);
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

  if (!file_saver_save_finish (saver, result, &error))
    {
      g_task_return_error (task, g_steal_pointer (&error));
    }
  else
    {
      self->was_restored = FALSE;
      g_task_return_boolean (task, TRUE);
    }

  _editor_document_unmark_busy (self);
}

void
_editor_document_save_draft_async (EditorDocument      *self,
                                   GCancellable        *cancellable,
                                   GAsyncReadyCallback  callback,
                                   gpointer             user_data)
{
  g_autoptr(GtkSourceFileSaver) saver = NULL;
  g_autoptr(GTask) task = NULL;
  g_autoptr(GFile) draft_file = NULL;
  g_autoptr(GFile) draft_dir = NULL;
  g_autoptr(GtkSourceFile) file = NULL;
  g_autofree gchar *title = NULL;
  g_autofree gchar *uri = NULL;
  EditorSession *session;

  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));
  g_return_if_fail (self->draft_id != NULL);

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, _editor_document_save_draft_async);

  if (!self->needs_autosave)
    {
      /* Do nothing if we are already up to date */
      g_task_return_boolean (task, TRUE);
      return;
    }

  self->needs_autosave = FALSE;

  /* First tell the session to track this draft */
  session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);
  title = editor_document_dup_title (self);
  uri = _editor_document_dup_uri (self);
  _editor_session_add_draft (session, self->draft_id, title, uri);

  /* Create a new GtkSourceFile to save the document so we don't
   * end up mutating what we have in self->file.
   */
  draft_file = editor_document_get_draft_file (self);
  file = gtk_source_file_new ();
  gtk_source_file_set_location (file, draft_file);
  saver = gtk_source_file_saver_new (GTK_SOURCE_BUFFER (self), file);
  gtk_source_file_saver_set_flags (saver,
                                   (GTK_SOURCE_FILE_SAVER_FLAGS_IGNORE_INVALID_CHARS |
                                    GTK_SOURCE_FILE_SAVER_FLAGS_IGNORE_MODIFICATION_TIME));
  gtk_source_file_saver_set_newline_type (saver, self->newline_type);

  if (self->encoding != NULL)
    gtk_source_file_saver_set_encoding (saver, self->encoding);

  /* TODO: Probably want to make this async. We can just create an
   * async variant in editor-utils.c for this.
   */
  draft_dir = g_file_get_parent (draft_file);
  g_file_make_directory_with_parents (draft_dir, cancellable, NULL);

  _editor_document_mark_busy (self);

  /* Ignore progress when saving the draft as it could confuse the
   * user about what is going on in the background.
   */
  gtk_source_file_saver_save_async (saver,
                                    G_PRIORITY_DEFAULT,
                                    cancellable,
                                    NULL, NULL, NULL,
                                    editor_document_save_draft_cb,
                                    g_steal_pointer (&task));
}

gboolean
_editor_document_save_draft_finish (EditorDocument  *self,
                                    GAsyncResult    *result,
                                    GError         **error)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);
  g_return_val_if_fail (G_IS_TASK (result), FALSE);

  return g_task_propagate_boolean (G_TASK (result), error);
}

static void
editor_document_query_etag_cb (GObject      *object,
                               GAsyncResult *result,
                               gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GFileInfo) info = NULL;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  EditorDocument *self;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  self->was_restored = FALSE;

  if ((info = g_file_query_info_finish (file, result, &error)))
    {
      const char *etag = g_file_info_get_etag (info);
      editor_buffer_monitor_set_etag (self->monitor, etag);
    }

  _editor_document_unmark_busy (self);
  _editor_document_set_externally_modified (self, FALSE);

  g_task_return_boolean (task, TRUE);
}

static void
editor_document_save_position_cb (GObject      *object,
                                  GAsyncResult *result,
                                  gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  if (!g_file_set_attributes_finish (file, result, NULL, &error))
    g_warning ("Failed to save cursor position: %s", error->message);

  /* Now query for the etag so we can update the buffer monitor to ensure
   * that it doesn't misfire if the file has not changed.
   */
  g_file_query_info_async (file,
                           G_FILE_ATTRIBUTE_ETAG_VALUE,
                           G_FILE_QUERY_INFO_NONE,
                           G_PRIORITY_DEFAULT,
                           NULL,
                           editor_document_query_etag_cb,
                           g_steal_pointer (&task));
}

static void
delete_draft_cb (GObject      *object,
                 GAsyncResult *result,
                 gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GError) error = NULL;

  g_assert (G_IS_FILE (object));
  g_assert (G_IS_ASYNC_RESULT (result));

  if (!g_file_delete_finish (file, result, &error))
    {
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        g_warning ("Failed to delete draft: %s", error->message);
    }
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
  g_autoptr(GFile) file = NULL;
  EditorDocument *self;
  GtkSourceFile *source_file;
  Save *save;

  g_assert (GTK_SOURCE_IS_FILE_SAVER (saver));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  save = g_task_get_task_data (task);
  source_file = gtk_source_file_saver_get_file (saver);

  /* We need a copy of file in case gtk_source_file_saver_save_finish()
   * causes us to loose our reference.
   */
  file = g_object_ref (gtk_source_file_get_location (source_file));

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

  /* Make sure we don't autosave, we just saved the file */
  self->needs_autosave = FALSE;

  /* Delete the draft in case we had one */
  draft = editor_document_get_draft_file (self);
  g_file_delete_async (draft, G_PRIORITY_DEFAULT, NULL, delete_draft_cb, NULL);

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
_editor_document_save_async (EditorDocument      *self,
                             GFile               *file,
                             GCancellable        *cancellable,
                             GAsyncReadyCallback  callback,
                             gpointer             user_data)
{
  g_autoptr(GtkSourceFileSaver) saver = NULL;
  g_autoptr(GTask) task = NULL;
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
  g_task_set_source_tag (task, _editor_document_save_async);
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
  gtk_source_file_saver_set_newline_type (saver, self->newline_type);

  if (self->encoding != NULL)
    gtk_source_file_saver_set_encoding (saver, self->encoding);

  _editor_document_mark_busy (self);

  editor_document_set_busy_progress (self, 0, 2, .25);

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
_editor_document_save_finish (EditorDocument  *self,
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
      editor_buffer_monitor_pause (self->monitor);
    }
}

void
_editor_document_unmark_busy (EditorDocument *self)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (self->busy_count > 0);

  self->busy_count--;

  if (self->busy_count == 0)
    {
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_BUSY]);
      editor_buffer_monitor_unpause (self->monitor);
    }
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

  editor_buffer_monitor_reset (self->monitor);

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
  self->needs_autosave = FALSE;

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

      self->newline_type = gtk_source_file_loader_get_newline_type (loader);

      _editor_document_set_externally_modified (self, FALSE);

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
      /* We are creating a new file. */
      editor_document_set_busy_progress (self, 1, 2, 1.0);
      _editor_document_unmark_busy (self);
      gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (self), TRUE);
      g_task_return_boolean (task, TRUE);
      return;
    }

  editor_document_set_busy_progress (self, 0, 2, 1.0);

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

  /* Keep a note about if the contents came from a draft
   * so that we can show it to the user when displaying
   * the document.
   */
  self->was_restored = load->has_draft;

  loader = gtk_source_file_loader_new (GTK_SOURCE_BUFFER (self), file);

  if (self->encoding != NULL)
    {
      GSList encodings = { .next = NULL, .data = (gpointer)self->encoding };
      gtk_source_file_loader_set_candidate_encodings (loader, &encodings);
    }

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
      const char *etag = g_file_info_get_etag (info);

      load->modified_at = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_TIME_MODIFIED);
      load->has_draft = TRUE;

      editor_buffer_monitor_set_etag (self->monitor, etag);
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
      else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          load->has_file = FALSE;
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
  EditorDocument *self;
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
  self = g_task_get_source_object (task);

  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (load != NULL);
  g_assert (G_IS_FILE (load->file));
  g_assert (load->n_active > 0);

  editor_document_set_busy_progress (self, 0, 2, .5);

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

  editor_document_set_busy_progress (self, 0, 2, .25);

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
                           G_FILE_ATTRIBUTE_ETAG_VALUE","
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
 * _editor_document_guess_language_async:
 * @self: a #EditorDocument
 * @cancellable: (nullable): a #GCancellable
 * @callback: a #GAsyncReadyCallback
 * @user_data: closure data for @callback
 *
 * Asynchronously guesses the language for @self using the current filename
 * and content-type of the file.
 */
void
_editor_document_guess_language_async (EditorDocument      *self,
                                       GCancellable        *cancellable,
                                       GAsyncReadyCallback  callback,
                                       gpointer             user_data)
{
  g_autoptr(GTask) task = NULL;
  GFile *file;

  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (!cancellable || G_IS_CANCELLABLE (cancellable));

  task = g_task_new (self, cancellable, callback, user_data);
  g_task_set_source_tag (task, _editor_document_guess_language_async);

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
 * _editor_document_guess_language_finish:
 * @self: a #EditorDocument
 * @result: the #GAsyncResult provided to callback
 * @error: a location for a #GError, or %NULL
 *
 * Returns: %TRUE if successful; otherwise %FALSE and @error is set
 */
gboolean
_editor_document_guess_language_finish (EditorDocument  *self,
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
editor_document_dup_title (EditorDocument *self)
{
  GtkTextIter iter;
  GString *str;
  GFile *file;

  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  file = editor_document_get_file (self);

  if (file != NULL)
    return g_file_get_basename (file);

  str = g_string_new (NULL);

  gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (self), &iter);

  if (!gtk_text_iter_is_end (&iter))
    {
      guint count = 0;

      do
        {
          gunichar ch;

          if (count >= TITLE_MAX_LEN)
            break;

          ch = gtk_text_iter_get_char (&iter);

          /* If we get double newlines, then assume we're at end of title */
          if (ch == '\n' && str->len > 0)
            {
              GtkTextIter peek = iter;

              if (gtk_text_iter_forward_char (&peek) &&
                  gtk_text_iter_get_char (&peek) == '\n')
                break;
            }

          if (g_unichar_isspace (ch) || !g_unichar_isalnum (ch))
            {
              if (count > TITLE_LAST_WORD_POS)
                break;

              if (str->len > 0 && str->str[str->len-1] != ' ')
                {
                  count++;
                  g_string_append_c (str, ' ');
                }
            }
          else
            {
              count++;
              g_string_append_unichar (str, ch);
            }
        }
      while (gtk_text_iter_forward_char (&iter));
    }

  if (str->len > 0 && str->str[str->len-1] == ' ')
    g_string_truncate (str, str->len - 1);

  if (self->readonly)
    {
      g_string_append_c (str, ' ');
      g_string_append (str, _("[Read-Only]"));
    }

  return g_string_free (str, str->len == 0);
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

gboolean
editor_document_get_externally_modified (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);

  return self->externally_modified;
}

void
_editor_document_set_externally_modified (EditorDocument *self,
                                          gboolean        externally_modified)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  externally_modified = !!externally_modified;

  if (externally_modified != self->externally_modified)
    {
      self->externally_modified = externally_modified;
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_EXTERNALLY_MODIFIED]);
      if (!self->externally_modified)
        editor_buffer_monitor_reset (self->monitor);
    }
}

void
_editor_document_set_was_restored (EditorDocument *self,
                                   gboolean        was_restored)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  self->was_restored = !!was_restored;
}

gboolean
_editor_document_get_was_restored (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);

  return self->was_restored;
}

const GtkSourceEncoding *
_editor_document_get_encoding (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  return self->encoding;
}

void
_editor_document_set_encoding (EditorDocument          *self,
                               const GtkSourceEncoding *encoding)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  self->encoding = encoding;
}

GtkSourceNewlineType
_editor_document_get_newline_type (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), 0);

  return self->newline_type;
}

void
_editor_document_set_newline_type (EditorDocument       *self,
                                   GtkSourceNewlineType  newline_type)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  self->newline_type = newline_type;
}

/**
 * editor_document_get_spell_checker:
 *
 * Gets the spellchecker to use for the document.
 *
 * Returns: (transfer none) (nullable): an #EditorSpellChecker
 */
EditorSpellChecker *
editor_document_get_spell_checker (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  return self->spell_checker;
}

/**
 * editor_document_set_spell_checker:
 * @self: an #EditorDocument
 * @spell_checker: an #EditorSpellChecker
 *
 * Sets the spell checker to use for the document.
 */
void
editor_document_set_spell_checker (EditorDocument     *self,
                                   EditorSpellChecker *spell_checker)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (!spell_checker || EDITOR_IS_SPELL_CHECKER (spell_checker));

  if (g_set_object (&self->spell_checker, spell_checker))
    {
      editor_text_buffer_spell_adapter_set_checker (self->spell_adapter, spell_checker);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SPELL_CHECKER]);
    }
}

void
_editor_document_attach_actions (EditorDocument *self,
                                 GtkWidget      *widget)
{
  g_autoptr(GPropertyAction) check = NULL;
  g_autoptr(GPropertyAction) language = NULL;
  g_autoptr(GSimpleActionGroup) group = NULL;

  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (GTK_IS_WIDGET (widget));

  group = g_simple_action_group_new ();

  language = g_property_action_new ("language", self->spell_adapter, "language");
  g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (language));

  check = g_property_action_new ("enabled", self->spell_adapter, "enabled");
  g_action_map_add_action (G_ACTION_MAP (group), G_ACTION (check));

  gtk_widget_insert_action_group (widget, "spelling", G_ACTION_GROUP (group));
}

gboolean
_editor_document_check_spelling (EditorDocument *self,
                                 const char     *word)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);

  if (self->spell_checker != NULL)
    return editor_spell_checker_check_word (self->spell_checker, word, -1);

  return TRUE;
}

char **
_editor_document_list_corrections (EditorDocument *self,
                                   const char     *word)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);
  g_return_val_if_fail (word != NULL, NULL);

  if (self->spell_checker == NULL)
    return NULL;

  return editor_spell_checker_list_corrections (self->spell_checker, word);
}

void
_editor_document_add_spelling_word (EditorDocument *self,
                                    const char     *word)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  if (self->spell_checker != NULL)
    editor_spell_checker_add_word (self->spell_checker, word);
}
