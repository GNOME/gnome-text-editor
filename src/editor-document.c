/* editor-document.c
 *
 * Copyright 2020-2023 Christian Hergert <chergert@redhat.com>
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

#include <glib/gi18n.h>

#include <libspelling.h>

#include "editor-application-private.h"
#include "editor-buffer-monitor-private.h"
#include "editor-document-private.h"
#include "editor-document-statistics.h"
#include "editor-encoding-model.h"
#include "editor-session-private.h"
#include "editor-window-private.h"

#define METADATA_CURSOR     "metadata::gte-cursor"
#define METADATA_SPELLING   "metadata::gte-spelling"
#define METADATA_SYNTAX     "metadata::gte-syntax"
#define TITLE_LAST_WORD_POS 20
#define TITLE_MAX_LEN       100
#define IS_LARGE_FILE(size) ((size) >= 1024UL*1024UL) /* 1MB */

struct _EditorDocument
{
  GtkSourceBuffer               parent_instance;

  EditorBufferMonitor          *monitor;
  GtkSourceFile                *file;
  gchar                        *draft_id;
  const GtkSourceEncoding      *encoding;
  GError                       *last_error;

  EditorDocumentStatistics     *statistics;

  SpellingChecker              *spell_checker;
  SpellingTextBufferAdapter    *spell_adapter;

  GtkTextMark                  *saved_insert_mark;
  GtkTextMark                  *saved_selection_bound_mark;

  GtkSourceNewlineType          newline_type;
  guint                         busy_count;
  gdouble                       busy_progress;

  guint                         loading : 1;
  guint                         readonly : 1;
  guint                         needs_autosave : 1;
  guint                         was_restored : 1;
  guint                         externally_modified : 1;
  guint                         suggest_admin : 1;
  guint                         load_failed : 1;
  guint                         did_shutdown : 1;
  guint                         inserting : 1;
  guint                         deleting : 1;
  guint                         supress_jump : 1;
};

typedef struct
{
  GFile *file;
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

  guint            highlight_matching_brackets : 1;
  guint            highlight_syntax : 1;
  guint            check_spelling : 1;
  guint            has_draft : 1;
  guint            has_file : 1;
} Load;

G_DEFINE_TYPE (EditorDocument, editor_document, GTK_SOURCE_TYPE_BUFFER)

enum {
  PROP_0,
  PROP_BUSY,
  PROP_BUSY_PROGRESS,
  PROP_ENCODING,
  PROP_ERROR,
  PROP_EXTERNALLY_MODIFIED,
  PROP_FILE,
  PROP_HAD_ERROR,
  PROP_ERROR_MESSAGE,
  PROP_LOADING,
  PROP_NEWLINE_TYPE,
  PROP_SPELL_CHECKER,
  PROP_STATISTICS,
  PROP_SUGGEST_ADMIN,
  PROP_TITLE,
  N_PROPS
};

enum {
  CURSOR_JUMPED,
  SAVE,
  N_SIGNALS
};

static GParamSpec *properties [N_PROPS];
static GSettings *shared_settings;
static guint signals [N_SIGNALS];

static gboolean
is_admin (EditorDocument *self)
{
  g_autofree char *uri = NULL;
  GFile *file;

  g_assert (EDITOR_IS_DOCUMENT (self));

  if (!(file = editor_document_get_file (self)))
    return FALSE;

  if (g_file_is_native (file))
    return FALSE;

  uri = g_file_get_uri (file);

  return g_str_has_prefix (uri, "admin:///");
}

static void
editor_document_track_error (EditorDocument *self,
                             const GError   *error)
{
  g_assert (EDITOR_IS_DOCUMENT (self));

  g_clear_error (&self->last_error);
  self->last_error = error ? g_error_copy (error) : NULL;

  self->load_failed = error != NULL;

  editor_buffer_monitor_set_failed (self->monitor, self->load_failed);

  if (error == NULL)
    {
      self->suggest_admin = FALSE;
    }

  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
    {
      if (!is_admin (self))
        {
          self->suggest_admin = TRUE;
        }
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SUGGEST_ADMIN]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ERROR_MESSAGE]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_HAD_ERROR]);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ERROR]);
}

static void
check_error (GObject      *object,
             GAsyncResult *result,
             gpointer      user_data)
{
  GFile *file = (GFile *)object;
  g_autoptr(GError) error = NULL;

  if (!g_file_set_attributes_finish (file, result, NULL, &error))
    g_warning ("%s", error->message);
}

void
_editor_document_persist_syntax_language (EditorDocument *self,
                                          const char     *language_id)
{
  g_autoptr(GFileInfo) info = NULL;
  GFile *file;

  g_assert (EDITOR_IS_DOCUMENT (self));

  /* Only persist the metadata if we have a backing file */
  if (!(file = editor_document_get_file (self)) || !g_file_is_native (file))
    return;

  info = g_file_info_new ();
  g_file_info_set_attribute_string (info, METADATA_SYNTAX, language_id ? language_id : "");
  g_file_set_attributes_async (file, info, G_FILE_QUERY_INFO_NONE,
                               G_PRIORITY_DEFAULT, NULL, check_error, NULL);
}

static void
on_spelling_language_changed_cb (EditorDocument  *self,
                                 GParamSpec      *pspec,
                                 SpellingChecker *spell_checker)
{
  g_autoptr(GFileInfo) info = NULL;
  const char *language_id;
  GFile *file;

  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (SPELLING_IS_CHECKER (spell_checker));

  /* Only persist the metadata if we have a backing file */
  if (!(file = editor_document_get_file (self)) || !g_file_is_native (file))
    return;

  /* Ignore if there is nothing to set */
  if (!(language_id = spelling_checker_get_language (spell_checker)))
    return;

  info = g_file_info_new ();
  g_file_info_set_attribute_string (info, METADATA_SPELLING, language_id);
  g_file_set_attributes_async (file, info, G_FILE_QUERY_INFO_NONE,
                               G_PRIORITY_DEFAULT, NULL, check_error, NULL);
}

static void
editor_document_emit_save (EditorDocument *self)
{
  g_assert (EDITOR_IS_DOCUMENT (self));

  g_signal_emit (self, signals [SAVE], 0);
}

static GtkSourceLanguage *
guess_language (GtkSourceLanguageManager *manager,
                const char               *filename,
                const char               *content_type)
{
  /* Apply content-type overrides */
  if (filename != NULL && content_type != NULL)
    {
      /* shared-mime-info calls it text/x-python rather than text/x-python3
       * if the shebang is something like "env python" vs "env python3". We
       * always want to default to Python 3.
       */
      if (g_str_has_suffix (filename, ".py") &&
          g_str_equal (content_type, "text/x-python"))
        content_type = "text/x-python3";
    }

  return gtk_source_language_manager_guess_language (manager, filename, content_type);
}

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
  g_clear_object (&save->file);
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
  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (G_IS_TASK (task));

  self->loading = FALSE;

  if (self->statistics != NULL)
    editor_document_statistics_reload (self->statistics);

  if (self->spell_adapter != NULL)
    spelling_text_buffer_adapter_invalidate_all (self->spell_adapter);

  if (!g_task_had_error (task))
    editor_document_track_error (self, NULL);

  _editor_session_document_seen (EDITOR_SESSION_DEFAULT, self);

  /* Notify position so that consumers update */
  g_signal_emit_by_name (self, "cursor-moved");

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_LOADING]);
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

  if (!self->loading)
    {
      /* Track separately from :modified for drafts */
      self->needs_autosave = TRUE;

      if (self->statistics != NULL)
        editor_document_statistics_queue_reload (self->statistics);
    }

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
  language = guess_language (manager, filename, content_type);

  /* Special case for Markdown which is extremely common and tends
   * to follow a '# title' format on the first line.
   */
  if (language == NULL)
    {
      if (content[0] == '#')
        {
          for (guint i = 1; content[i]; i++)
            {
              if (content[i] == ' ')
                {
                  language = gtk_source_language_manager_get_language (manager, "markdown");
                  break;
                }
            }
        }
    }

  if (language != NULL)
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

  g_assert (GTK_IS_TEXT_BUFFER (buffer));
  g_assert (pos != NULL);
  g_assert (new_text != NULL);

  /* Short-circuit during loading, we'll handle the followup
   * actions after loading has completed.
   */
  if (self->loading)
    {
      self->inserting = TRUE;
      GTK_TEXT_BUFFER_CLASS (editor_document_parent_class)->insert_text (buffer, pos, new_text, new_text_length);
      self->inserting = FALSE;
      return;
    }

  line = gtk_text_iter_get_line (pos);
  offset = gtk_text_iter_get_offset (pos);

  self->inserting = TRUE;
  GTK_TEXT_BUFFER_CLASS (editor_document_parent_class)->insert_text (buffer, pos, new_text, new_text_length);
  self->inserting = FALSE;

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

  g_assert (GTK_IS_TEXT_BUFFER (buffer));
  g_assert (start != NULL);
  g_assert (end != NULL);
  g_assert (gtk_text_iter_compare (start, end) <= 0);

  /* Unlikely, but short circuit during loading in-case something
   * beneath us has to delete/replace as part of resolving invalid
   * characters.
   */
  if (self->loading)
    {
      self->deleting = TRUE;
      GTK_TEXT_BUFFER_CLASS (editor_document_parent_class)->delete_range (buffer, start, end);
      self->deleting = FALSE;
      return;
    }

  offset = gtk_text_iter_get_offset (start);

  self->deleting = TRUE;
  GTK_TEXT_BUFFER_CLASS (editor_document_parent_class)->delete_range (buffer, start, end);
  self->deleting = FALSE;

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

static gboolean
apply_spellcheck_mapping (GValue   *value,
                          GVariant *variant,
                          gpointer  user_data)
{
  EditorDocument *self = user_data;

  g_assert (EDITOR_IS_DOCUMENT (self));

  /* Ignore while loading */
  if (self->loading)
    g_value_set_boolean (value, FALSE);
  else
    g_value_set_boolean (value, g_variant_get_boolean (variant));

  return TRUE;
}

static void
editor_document_cursor_moved (EditorDocument *self)
{
  g_assert (EDITOR_IS_DOCUMENT (self));

  if (self->inserting || self->deleting || self->supress_jump)
    return;

  g_signal_emit (self, signals[CURSOR_JUMPED], 0);
}

EditorEncoding *
editor_document_dup_encoding (EditorDocument *self)
{
  EditorEncoding *encoding;

  if ((encoding = editor_encoding_model_get (NULL, self->encoding)))
    return g_object_ref (encoding);

  return NULL;
}

void
editor_document_set_encoding (EditorDocument *self,
                              EditorEncoding *encoding)
{
  const GtkSourceEncoding *enc;

  if (encoding != NULL)
    enc = editor_encoding_get_encoding (encoding);
  else
    enc = gtk_source_encoding_get_utf8 ();

  _editor_document_set_encoding (self, enc);
}

static void
editor_document_notify_location_cb (EditorDocument *self,
                                    GParamSpec     *pspec,
                                    GtkSourceFile  *file)
{
  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (GTK_SOURCE_IS_FILE (file));

  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FILE]);
}

static void
editor_document_constructed (GObject *object)
{
  EditorDocument *self = (EditorDocument *)object;
  GtkTextIter iter;

  if (shared_settings == NULL)
    shared_settings = g_settings_new ("org.gnome.TextEditor");

  G_OBJECT_CLASS (editor_document_parent_class)->constructed (object);

  gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (self), &iter);
  self->saved_insert_mark = gtk_text_buffer_create_mark (GTK_TEXT_BUFFER (self),
                                                         NULL, &iter, TRUE);
  self->saved_selection_bound_mark = gtk_text_buffer_create_mark (GTK_TEXT_BUFFER (self),
                                                                  NULL, &iter, FALSE);

  self->spell_adapter = spelling_text_buffer_adapter_new (GTK_SOURCE_BUFFER (self),
                                                          self->spell_checker);
  g_settings_bind_with_mapping (shared_settings, "spellcheck",
                                self->spell_adapter, "enabled",
                                G_SETTINGS_BIND_GET,
                                apply_spellcheck_mapping, NULL, self, NULL);
}

static void
editor_document_finalize (GObject *object)
{
  EditorDocument *self = (EditorDocument *)object;

  g_clear_object (&self->statistics);
  g_clear_object (&self->monitor);
  g_clear_object (&self->file);
  g_clear_object (&self->spell_checker);
  g_clear_object (&self->spell_adapter);
  g_clear_error (&self->last_error);
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
    case PROP_SUGGEST_ADMIN:
      g_value_set_boolean (value, self->suggest_admin);
      break;

    case PROP_HAD_ERROR:
      g_value_set_boolean (value, _editor_document_had_error (self));
      break;

    case PROP_ERROR_MESSAGE:
      if (self->last_error != NULL)
        g_value_set_string (value, self->last_error->message);
      break;

    case PROP_BUSY:
      g_value_set_boolean (value, editor_document_get_busy (self));
      break;

    case PROP_BUSY_PROGRESS:
      g_value_set_double (value, editor_document_get_busy_progress (self));
      break;

    case PROP_ENCODING:
      g_value_take_object (value, editor_document_dup_encoding (self));
      break;

    case PROP_ERROR:
      g_value_set_boxed (value, self->last_error);
      break;

    case PROP_EXTERNALLY_MODIFIED:
      g_value_set_boolean (value, editor_document_get_externally_modified (self));
      break;

    case PROP_FILE:
      g_value_set_object (value, editor_document_get_file (self));
      break;

    case PROP_LOADING:
      g_value_set_boolean (value, _editor_document_get_loading (self));
      break;

    case PROP_NEWLINE_TYPE:
      g_value_set_enum (value, self->newline_type);
      break;

    case PROP_STATISTICS:
      g_value_take_object (value, editor_document_load_statistics (self));
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
    case PROP_ENCODING:
      editor_document_set_encoding (self, g_value_get_object (value));
      break;

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

  properties [PROP_SUGGEST_ADMIN] =
    g_param_spec_boolean ("suggest-admin",
                          "Suggest Admin",
                          "Suggest to the user to use admin://",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_HAD_ERROR] =
    g_param_spec_boolean ("had-error",
                          "Had Error",
                          "If there was an error with the document",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ERROR_MESSAGE] =
    g_param_spec_string ("error-message",
                          "Error Message",
                          "Error message to be used when Had Error is set",
                          NULL,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ERROR] =
    g_param_spec_boxed ("error", NULL, NULL,
                        G_TYPE_ERROR,
                        (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

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

  properties [PROP_ENCODING] =
    g_param_spec_object ("encoding", NULL, NULL,
                         EDITOR_TYPE_ENCODING,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

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

  properties [PROP_LOADING] =
    g_param_spec_boolean ("loading",
                          "Loading",
                          "If the document is currently loading",
                          FALSE,
                          (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties [PROP_SPELL_CHECKER] =
    g_param_spec_object ("spell-checker",
                         "Spell Checker",
                         "Spell Checker",
                         SPELLING_TYPE_CHECKER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_STATISTICS] =
    g_param_spec_object ("statistics", NULL, NULL,
                         EDITOR_TYPE_DOCUMENT_STATISTICS,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties [PROP_TITLE] =
    g_param_spec_string ("title",
                         "Title",
                         "The title for the document",
                         NULL,
                         (G_PARAM_READABLE | G_PARAM_STATIC_STRINGS));

  properties[PROP_NEWLINE_TYPE] =
    g_param_spec_enum ("newline-type", NULL, NULL,
                       GTK_SOURCE_TYPE_NEWLINE_TYPE,
                       GTK_SOURCE_NEWLINE_TYPE_DEFAULT,
                       (G_PARAM_READABLE |
                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  /**
   * EditorDocument::save:
   * @self: a #EditorDocument
   *
   * The "save" signal is emitted right before the document
   * is about to be saved.
   */
  signals [SAVE] = g_signal_new ("save",
                                 G_TYPE_FROM_CLASS (klass),
                                 G_SIGNAL_RUN_LAST,
                                 0,
                                 NULL, NULL,
                                 NULL,
                                 G_TYPE_NONE, 0);

  signals [CURSOR_JUMPED] = g_signal_new ("cursor-jumped",
                                          G_TYPE_FROM_CLASS (klass),
                                          G_SIGNAL_RUN_LAST,
                                          0,
                                          NULL, NULL,
                                          NULL,
                                          G_TYPE_NONE, 0);
}

static void
editor_document_init (EditorDocument *self)
{
  g_autoptr(SpellingChecker) spell_checker = spelling_checker_new (NULL, NULL);

  self->newline_type = GTK_SOURCE_NEWLINE_TYPE_DEFAULT;
  self->file = gtk_source_file_new ();
  self->draft_id = g_uuid_string_random ();

  g_signal_connect_object (self->file,
                           "notify::location",
                           G_CALLBACK (editor_document_notify_location_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect (self,
                    "cursor-moved",
                    G_CALLBACK (editor_document_cursor_moved),
                    self);

  /* Default implicit-trailing-newline to on in case there are
   * no automatic settings providers.
   */
  gtk_source_buffer_set_implicit_trailing_newline (GTK_SOURCE_BUFFER (self), TRUE);

  editor_document_set_spell_checker (self, spell_checker);

  self->monitor = editor_buffer_monitor_new ();
  g_signal_connect_object (self->monitor,
                           "notify::changed",
                           G_CALLBACK (editor_document_monitor_notify_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_object_bind_property (self->file, "location",
                          self->monitor, "file",
                          G_BINDING_SYNC_CREATE);
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

  if (!self->needs_autosave || self->loading)
    {
      /* Do nothing if we are already up to date */
      g_task_return_boolean (task, TRUE);
      return;
    }

  editor_document_emit_save (self);

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

  /* If there are no modifications, then delete any backing file
   * for the draft so we don't risk reloading it.
   */
  if (!gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (self)) &&
      editor_document_get_file (self) != NULL)
    {
      g_file_delete (draft_file, NULL, NULL);
      g_task_return_boolean (task, TRUE);
      return;
    }

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
  EditorDocument *self;
  GtkSourceFile *source_file;
  Save *save;

  g_assert (GTK_SOURCE_IS_FILE_SAVER (saver));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (task));

  self = g_task_get_source_object (task);
  save = g_task_get_task_data (task);
  source_file = gtk_source_file_saver_get_file (saver);

  g_assert (EDITOR_IS_DOCUMENT (self));
  g_assert (GTK_SOURCE_IS_FILE (source_file));
  g_assert (G_IS_FILE (save->file));
  g_assert (save != NULL);

  if (!gtk_source_file_saver_save_finish (saver, result, &error))
    {
      g_autofree char *uri = g_file_get_uri (save->file);

      /* Try again as admin:// */
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED) &&
          g_str_has_prefix (uri, "file://") &&
          !g_object_get_data (G_OBJECT (saver), "IS_ADMIN"))
        {
          g_autofree char *admin_uri = g_strdup_printf ("admin://%s", uri + strlen ("file://"));
          g_autoptr(GFile) admin_file = g_file_new_for_uri (admin_uri);
          g_autoptr(GtkSourceFileSaver) admin_saver = NULL;

          gtk_source_file_set_location (source_file, admin_file);

          admin_saver = gtk_source_file_saver_new (GTK_SOURCE_BUFFER (self), source_file);
          gtk_source_file_saver_set_compression_type (admin_saver, gtk_source_file_saver_get_compression_type (saver));
          gtk_source_file_saver_set_encoding (admin_saver, gtk_source_file_saver_get_encoding (saver));
          gtk_source_file_saver_set_flags (admin_saver, gtk_source_file_saver_get_flags (saver));
          gtk_source_file_saver_set_newline_type (admin_saver, gtk_source_file_saver_get_newline_type (saver));

          g_object_set_data (G_OBJECT (admin_saver), "IS_ADMIN", GINT_TO_POINTER (TRUE));

          gtk_source_file_saver_save_async (admin_saver,
                                            G_PRIORITY_DEFAULT,
                                            NULL,
                                            editor_document_progress,
                                            self,
                                            NULL,
                                            editor_document_save_cb,
                                            g_steal_pointer (&task));

          return;
        }

      _editor_document_unmark_busy (self);
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  /* Make sure we don't autosave, we just saved the file */
  self->needs_autosave = FALSE;

  /* Delete the draft in case we had one */
  draft = editor_document_get_draft_file (self);
  g_file_delete_async (draft, G_PRIORITY_DEFAULT, NULL, delete_draft_cb, NULL);

  /* gtk_source_file_saver_save_finish() may cause our GFile to change
   * if we did a save as operation. Make sure we save state on the new
   * file instead of the old file (which is currently @file).
   */
  info = g_file_info_new ();
  g_file_info_set_attribute_string (info, METADATA_CURSOR, save->position);
  g_file_set_attributes_async (editor_document_get_file (self),
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

  editor_document_emit_save (self);

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

  save->file = g_object_ref (file);

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

      if (self->monitor != NULL)
        editor_buffer_monitor_unpause (self->monitor);

      /* Now that we are not busy, emit cursor-moved so that anything that
       * wants to watch the cursor position can safely ignore changes while
       * the document is busy.
       */
      self->supress_jump = TRUE;
      g_signal_emit_by_name (self, "cursor-moved");
      self->supress_jump = FALSE;
    }
}

gboolean
_editor_document_get_loading (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);

  return self->loading;
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
  GtkSourceLanguage *language = NULL;
  EditorDocument *self;
  const gchar *content_type;
  const gchar *position;
  const gchar *spelling_language;
  const gchar *syntax;
  gboolean readonly;
  gboolean is_modified;
  goffset size;
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
  position = g_file_info_get_attribute_string (info, METADATA_CURSOR);
  spelling_language = g_file_info_get_attribute_string (info, METADATA_SPELLING);
  syntax = g_file_info_get_attribute_string (info, METADATA_SYNTAX);
  size = g_file_info_get_size (info);

  editor_document_set_readonly (self, readonly);

  /* Use existing language from extended attribute if it is available, otherwise
   * fallback to guessing the syntax from the content-type.
   */
  if (syntax != NULL && syntax[0] != 0)
    language = gtk_source_language_manager_get_language (lm, syntax);
  if (language == NULL)
    language = guess_language (lm, filename, content_type);

  gtk_source_buffer_set_language (GTK_SOURCE_BUFFER (self), language);
  gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (self), language != NULL);

  /* If the cursor position was provided at app startup, then steal
   * that position from the hashtable and restore it.
   *
   * Otherwise, if the metadata for the file has the position, use that
   * so that the session looks similar to when we closed.
   */
  if (_editor_application_consume_position (EDITOR_APPLICATION_DEFAULT, file, &line, &line_offset))
    {
      GtkTextIter iter;

      g_debug ("Moving cursor to requested position %u:%u",
               line, line_offset);

      if (line == 0)
        gtk_text_buffer_get_end_iter (GTK_TEXT_BUFFER (self), &iter);
      else
        gtk_text_buffer_get_iter_at_line_offset (GTK_TEXT_BUFFER (self),
                                                 &iter,
                                                 line - 1,
                                                 line_offset > 0 ? line_offset - 1 : 0);

      /* Move to first non-space character if offset is not provided */
      if (line_offset == 0)
        {
          while (!gtk_text_iter_ends_line (&iter) &&
                 g_unichar_isspace (gtk_text_iter_get_char (&iter)))
            {
              if (!gtk_text_iter_forward_char (&iter))
                break;
            }
        }

      gtk_text_buffer_select_range (GTK_TEXT_BUFFER (self), &iter, &iter);
    }
  else if (position != NULL &&
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

  /* Apply metadata for spelling language */
  if (spelling_language != NULL && self->spell_checker != NULL)
    spelling_checker_set_language (self->spell_checker, spelling_language);

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

  /* Check to see if we should disable features that are likely to
   * cause problems on large buffers. We'll consider anything >= 1MB
   * a "large file" from this standpoint.
   */
  if (IS_LARGE_FILE (size))
    {
      g_autofree char *title = editor_document_dup_title (self);
      g_autofree char *sizestr = g_format_size (size);

      g_debug ("Disabling features for buffer \"%s\" due to size (%s)",
               title, sizestr);

      /* Document settings may try to override these as the load completes
       * so we need to clear them again.
       */
      gtk_source_buffer_set_highlight_matching_brackets (GTK_SOURCE_BUFFER (self), FALSE);
      gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (self), FALSE);
      spelling_text_buffer_adapter_set_enabled (self->spell_adapter, FALSE);
    }
  else
    {
      gtk_source_buffer_set_highlight_matching_brackets (GTK_SOURCE_BUFFER (self), load->highlight_matching_brackets);
      gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (self), load->highlight_syntax);
      spelling_text_buffer_adapter_set_enabled (self->spell_adapter, load->check_spelling);
    }

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
      editor_document_track_error (self, error);
      g_task_return_error (task, g_steal_pointer (&error));
      _editor_document_unmark_busy (self);
      return;
    }
  else
    {
      const GtkSourceEncoding *encoding;
      GFile *file = editor_document_get_file (self);
      g_autoptr(GFile) draft_file = NULL;
      GtkTextIter begin;

      g_assert (!file || G_IS_FILE (file));

      _editor_document_set_newline_type (self, gtk_source_file_loader_get_newline_type (loader));

      /* We want to keep the same encoding when saving that was
       * auto-detected from loading the file.
       *
       * See #743
       */
      if ((encoding = gtk_source_file_loader_get_encoding (loader)))
        self->encoding = encoding;

      _editor_document_set_externally_modified (self, FALSE);

      gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (self), &begin);
      gtk_text_buffer_select_range (GTK_TEXT_BUFFER (self), &begin, &begin);

      if (file == NULL)
        file = draft_file = editor_document_get_draft_file (self);

      g_file_query_info_async (file,
                               G_FILE_ATTRIBUTE_STANDARD_SIZE","
                               G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE","
                               G_FILE_ATTRIBUTE_FILESYSTEM_READONLY","
                               METADATA_CURSOR","
                               METADATA_SYNTAX","
                               METADATA_SPELLING,
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
      gtk_source_buffer_set_highlight_matching_brackets (GTK_SOURCE_BUFFER (self), load->highlight_matching_brackets);
      gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (self), load->highlight_syntax);
      spelling_text_buffer_adapter_set_enabled (self->spell_adapter, load->check_spelling);
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
                                     G_PRIORITY_LOW,
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
          g_clear_error (&error);
        }
      else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
        {
          load->has_file = FALSE;
          load->content_type = NULL;
          load->modified_at = 0;
          g_clear_error (&error);
        }
      else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED))
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

  /* XXX: We might need to be more careful about how we call this multiple times */
  editor_document_track_error (self, error);

  load->n_active--;

  if (load->n_active == 0)
    {
      if (error != NULL)
        g_task_return_error (task, g_steal_pointer (&error));
      else if (self->last_error)
        g_task_return_error (task, g_error_copy (self->last_error));
      else
        editor_document_do_load (self, task, load);
    }
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
  g_return_if_fail (self->loading == FALSE);

  if (cancellable == NULL && window != NULL)
    cancellable = _editor_window_get_cancellable (window);

  self->loading = TRUE;
  self->needs_autosave = FALSE;

  file = editor_document_get_file (self);

  load = g_slice_new0 (Load);
  load->file = file ? g_file_dup (file) : NULL;
  load->draft_file = editor_document_get_draft_file (self);

  if (window != NULL)
    load->mount_operation = gtk_mount_operation_new (GTK_WINDOW (window));
  else
    load->mount_operation = g_mount_operation_new ();

  load->highlight_matching_brackets =
      gtk_source_buffer_get_highlight_matching_brackets (GTK_SOURCE_BUFFER (self));
  load->highlight_syntax =
      gtk_source_buffer_get_highlight_syntax (GTK_SOURCE_BUFFER (self));
  load->check_spelling =
      spelling_text_buffer_adapter_get_enabled (self->spell_adapter);

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
  gtk_source_buffer_set_highlight_matching_brackets (GTK_SOURCE_BUFFER (self), FALSE);
  gtk_source_buffer_set_highlight_syntax (GTK_SOURCE_BUFFER (self), FALSE);
  spelling_text_buffer_adapter_set_enabled (self->spell_adapter, FALSE);

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

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_LOADING]);
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
  GtkSourceLanguage *language = NULL;
  EditorDocument *self;
  const gchar *content_type;
  const char *syntax;

  g_assert (G_IS_FILE (file));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (G_IS_TASK (result));

  if (!(info = g_file_query_info_finish (file, result, &error)))
    {
      g_task_return_error (task, g_steal_pointer (&error));
      return;
    }

  lm = gtk_source_language_manager_get_default ();

  if ((syntax = g_file_info_get_attribute_string (info, METADATA_SYNTAX)))
    language = gtk_source_language_manager_get_language (lm, syntax);

  if (language == NULL)
    {
      content_type = g_file_info_get_attribute_string (info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
      filename = g_file_get_basename (file);
      language = guess_language (lm, filename, content_type);
    }

  self = g_task_get_source_object (task);

  g_assert (EDITOR_IS_DOCUMENT (self));
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
  else if (self->load_failed)
    g_task_return_new_error (task,
                             G_IO_ERROR,
                             G_IO_ERROR_CANCELLED,
                             "Cannot query file as load failed.");
  else
    g_file_query_info_async (file,
                             G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE","
                             METADATA_SYNTAX,
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
  str = g_string_new (NULL);

  if (file != NULL)
    {
      g_autofree char *base = g_file_get_basename (file);

      if (!g_utf8_validate (base, -1, NULL))
        {
          char *tmp = g_utf8_make_valid (base, -1);

          g_free (base);
          base = tmp;
        }

      g_string_append (str, base);
      goto handle_suffix;
    }

  gtk_text_buffer_get_start_iter (GTK_TEXT_BUFFER (self), &iter);

  if (!gtk_text_iter_is_end (&iter))
    {
      guint count = 0;

      do
        {
          GUnicodeScript script;
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

          script = g_unichar_get_script (ch);

          if (g_unichar_isspace (ch) ||
              (((script == G_UNICODE_SCRIPT_COMMON) || (script == G_UNICODE_SCRIPT_LATIN)) && !g_unichar_isalnum (ch)))
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

handle_suffix:

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

  if (encoding != self->encoding)
    {
      self->encoding = encoding;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_ENCODING]);
    }
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

  if (newline_type != self->newline_type)
    {
      self->newline_type = newline_type;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NEWLINE_TYPE]);
    }
}

/**
 * editor_document_get_spell_checker:
 *
 * Gets the spellchecker to use for the document.
 *
 * Returns: (transfer none) (nullable): an #SpellingChecker
 */
SpellingChecker *
editor_document_get_spell_checker (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  return self->spell_checker;
}

/**
 * editor_document_set_spell_checker:
 * @self: an #EditorDocument
 * @spell_checker: an #SpellingChecker
 *
 * Sets the spell checker to use for the document.
 */
void
editor_document_set_spell_checker (EditorDocument  *self,
                                   SpellingChecker *spell_checker)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));
  g_return_if_fail (!spell_checker || SPELLING_IS_CHECKER (spell_checker));

  if (spell_checker == self->spell_checker)
    return;

  if (self->spell_checker != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->spell_checker,
                                            G_CALLBACK (on_spelling_language_changed_cb),
                                            self);
      g_clear_object (&self->spell_checker);
    }

  if (spell_checker != NULL)
    {
      self->spell_checker = g_object_ref (spell_checker);
      g_signal_connect_object (self->spell_checker,
                               "notify::language",
                               G_CALLBACK (on_spelling_language_changed_cb),
                               self,
                               G_CONNECT_SWAPPED);
    }

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_SPELL_CHECKER]);
}

void
_editor_document_use_admin (EditorDocument *self,
                            EditorWindow   *window)
{
  GFile *file;
  g_autofree char *uri = NULL;
  g_autofree char *admin_uri = NULL;
  g_autoptr(GFile) admin_file = NULL;

  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  if (!(file = editor_document_get_file (self)))
    {
      g_warning ("No file, cannot change to admin:// protocol");
      return;
    }

  uri = g_file_get_uri (file);

  if (!g_str_has_prefix (uri, "file:///"))
    {
      g_warning ("URI \"%s\" does not start with \"file:///\"", uri);
      return;
    }

  admin_uri = g_strdup_printf ("admin://%s", g_file_get_path (file));
  admin_file = g_file_new_for_uri (admin_uri);

  g_debug ("Changing URI from \"%s\" to \"%s\"", uri, admin_uri);
  gtk_source_file_set_location (self->file, admin_file);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_FILE]);

  _editor_document_load_async (self, window, NULL, NULL, NULL);
}

gboolean
_editor_document_had_error (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);

  return self->load_failed;
}

char *
_editor_document_suggest_filename (EditorDocument *self)
{
  g_autofree char *title = NULL;
  GtkSourceLanguage *lang;
  const char *suggested_name = NULL;
  const char *suggested_suffix = NULL;
  const char *current_suffix;

  g_assert (EDITOR_IS_DOCUMENT (self));

  if ((lang = gtk_source_buffer_get_language (GTK_SOURCE_BUFFER (self))))
    {
      suggested_suffix = gtk_source_language_get_metadata (lang, "suggested-suffix");
      suggested_name = gtk_source_language_get_metadata (lang, "suggested-name");
    }

  if (suggested_name != NULL)
    return g_strdup (suggested_name);

  if (suggested_suffix == NULL)
    suggested_suffix = ".txt";

  title = editor_document_dup_title (self);

  if (title == NULL)
    title = g_strdup (_("New Document"));

  current_suffix = strrchr (title, '.');

  if (g_strcmp0 (current_suffix, suggested_suffix) == 0)
    return g_steal_pointer (&title);

  return g_strdup_printf ("%s%s", title, suggested_suffix);
}

GFile *
_editor_document_suggest_file (EditorDocument *self,
                               GFile          *directory)
{
  static GFile *documents;
  g_autofree char *name = NULL;
  const char *documents_dir;

  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);
  g_return_val_if_fail (!directory || G_IS_FILE (directory), NULL);

  documents_dir = g_get_user_special_dir (G_USER_DIRECTORY_DOCUMENTS);

  if (documents_dir == NULL)
    {
      g_warning_once ("Your system has an improperly configured XDG_DOCUMENTS_DIR. Using $HOME instead.");
      documents_dir = g_get_home_dir ();
    }

  if (directory == NULL)
    {
      if (documents == NULL)
        documents = g_file_new_for_path (documents_dir);
      directory = documents;
    }

  name = _editor_document_suggest_filename (self);

  g_assert (G_IS_FILE (directory));
  g_assert (name != NULL);

  return g_file_get_child (directory, name);
}

gboolean
_editor_document_did_shutdown (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), FALSE);

  return self->did_shutdown;
}

void
_editor_document_shutdown (EditorDocument *self)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  self->did_shutdown = TRUE;

  g_clear_object (&self->spell_checker);
  g_clear_object (&self->spell_adapter);
  g_clear_object (&self->monitor);
  g_clear_object (&self->file);
}

void
editor_document_update_corrections (EditorDocument *self)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  if (self->spell_adapter != NULL)
    spelling_text_buffer_adapter_update_corrections (self->spell_adapter);
}

GMenuModel *
editor_document_get_spelling_menu (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  if (self->spell_adapter != NULL)
    return spelling_text_buffer_adapter_get_menu_model (self->spell_adapter);

  return NULL;
}

GtkTextTag *
_editor_document_get_spelling_tag (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  if (self->spell_adapter != NULL)
    return spelling_text_buffer_adapter_get_tag (self->spell_adapter);

  return NULL;
}

void
_editor_document_attach_actions (EditorDocument *self,
                                 GtkWidget      *widget)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  if (self->spell_adapter != NULL)
    gtk_widget_insert_action_group (widget,
                                    "spelling",
                                    G_ACTION_GROUP (self->spell_adapter));
}

EditorDocumentStatistics *
editor_document_load_statistics (EditorDocument *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (self), NULL);

  if (self->statistics == NULL)
    {
      self->statistics = editor_document_statistics_new (self);
      editor_document_statistics_queue_reload (self->statistics);
    }

  return g_object_ref (self->statistics);
}

void
_editor_document_save_insert_mark (EditorDocument *self)
{
  GtkTextMark *insert;
  GtkTextMark *selection_bound;
  GtkTextIter iter;

  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  insert = gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (self));
  selection_bound = gtk_text_buffer_get_selection_bound (GTK_TEXT_BUFFER (self));

  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (self), &iter, insert);
  gtk_text_buffer_move_mark (GTK_TEXT_BUFFER (self), self->saved_insert_mark, &iter);

  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (self), &iter, selection_bound);
  gtk_text_buffer_move_mark (GTK_TEXT_BUFFER (self), self->saved_selection_bound_mark, &iter);
}

void
_editor_document_restore_insert_mark (EditorDocument *self)
{
  GtkTextIter iter;

  g_return_if_fail (EDITOR_IS_DOCUMENT (self));

  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (self), &iter, self->saved_insert_mark);
  gtk_text_buffer_move_mark (GTK_TEXT_BUFFER (self),
                             gtk_text_buffer_get_insert (GTK_TEXT_BUFFER (self)),
                             &iter);

  gtk_text_buffer_get_iter_at_mark (GTK_TEXT_BUFFER (self), &iter, self->saved_selection_bound_mark);
  gtk_text_buffer_move_mark (GTK_TEXT_BUFFER (self),
                             gtk_text_buffer_get_selection_bound (GTK_TEXT_BUFFER (self)),
                             &iter);
}
