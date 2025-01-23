/*
 * editor-document-statistics.c
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

#include "editor-document-statistics.h"

#include "line-reader-private.h"

#define QUEUED_RELOAD_DELAY 1000

struct _EditorDocumentStatistics
{
  GObject       parent_instance;
  GWeakRef      document_wr;
  GCancellable *cancellable;
  gsize         n_chars;
  gsize         n_lines;
  gsize         n_printable;
  gsize         n_spaces;
  gsize         n_words;
  guint         queued_reload;
};

enum {
  PROP_0,
  PROP_DOCUMENT,
  PROP_N_CHARS,
  PROP_N_LINES,
  PROP_N_PRINTABLE,
  PROP_N_SPACES,
  PROP_N_WORDS,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorDocumentStatistics, editor_document_statistics, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static void
editor_document_statistics_cancel (EditorDocumentStatistics *self)
{
  g_autoptr(GCancellable) cancellable = NULL;

  g_assert (EDITOR_IS_DOCUMENT_STATISTICS (self));

  cancellable = g_cancellable_new ();

  g_cancellable_cancel (self->cancellable);
  g_set_object (&self->cancellable, cancellable);
}

static void
editor_document_statistics_set_document (EditorDocumentStatistics *self,
                                         EditorDocument           *document)
{
  g_assert (EDITOR_IS_DOCUMENT_STATISTICS (self));
  g_assert (!document || EDITOR_IS_DOCUMENT (document));

  g_weak_ref_set (&self->document_wr, document);

  if (document != NULL)
    editor_document_statistics_reload (self);
}


static void
editor_document_statistics_finalize (GObject *object)
{
  EditorDocumentStatistics *self = (EditorDocumentStatistics *)object;

  g_clear_handle_id (&self->queued_reload, g_source_remove);
  g_weak_ref_clear (&self->document_wr);
  g_clear_object (&self->cancellable);

  G_OBJECT_CLASS (editor_document_statistics_parent_class)->finalize (object);
}

static void
editor_document_statistics_get_property (GObject    *object,
                                         guint       prop_id,
                                         GValue     *value,
                                         GParamSpec *pspec)
{
  EditorDocumentStatistics *self = EDITOR_DOCUMENT_STATISTICS (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_take_object (value, editor_document_statistics_dup_document (self));
      break;

    case PROP_N_CHARS:
      g_value_set_uint64 (value, self->n_chars);
      break;

    case PROP_N_LINES:
      g_value_set_uint64 (value, self->n_lines);
      break;

    case PROP_N_PRINTABLE:
      g_value_set_uint64 (value, self->n_printable);
      break;

    case PROP_N_SPACES:
      g_value_set_uint64 (value, self->n_spaces);
      break;

    case PROP_N_WORDS:
      g_value_set_uint64 (value, self->n_words);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_document_statistics_set_property (GObject      *object,
                                         guint         prop_id,
                                         const GValue *value,
                                         GParamSpec   *pspec)
{
  EditorDocumentStatistics *self = EDITOR_DOCUMENT_STATISTICS (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      editor_document_statistics_set_document (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_document_statistics_class_init (EditorDocumentStatisticsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = editor_document_statistics_finalize;
  object_class->get_property = editor_document_statistics_get_property;
  object_class->set_property = editor_document_statistics_set_property;

  properties[PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         EDITOR_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_N_CHARS] =
    g_param_spec_uint64 ("n-chars", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_N_LINES] =
    g_param_spec_uint64 ("n-lines", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_N_PRINTABLE] =
    g_param_spec_uint64 ("n-printable", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_N_SPACES] =
    g_param_spec_uint64 ("n-spaces", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_N_WORDS] =
    g_param_spec_uint64 ("n-words", NULL, NULL,
                         0, G_MAXUINT64, 0,
                         (G_PARAM_READABLE |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_document_statistics_init (EditorDocumentStatistics *self)
{
}

typedef struct _Worker
{
  GWeakRef       statistics_wr;
  PangoLanguage *language;
  GCancellable  *cancellable;
  char          *text;
  gsize          n_chars;
  gsize          n_lines;
  gsize          n_printable;
  gsize          n_spaces;
  gsize          n_words;
} Worker;

static void
worker_free (Worker *state)
{
  state->language = NULL;
  g_weak_ref_clear (&state->statistics_wr);
  g_clear_pointer (&state->text, g_free);
  g_clear_object (&state->cancellable);
  g_free (state);
}

static gboolean
editor_document_statistics_apply (gpointer data)
{
  g_autoptr(EditorDocumentStatistics) self = NULL;
  Worker *state = data;

  if ((self = g_weak_ref_get (&state->statistics_wr)) &&
      self->cancellable == state->cancellable)
    {
      self->n_chars = state->n_chars;
      self->n_lines = state->n_lines;
      self->n_printable = state->n_printable;
      self->n_spaces = state->n_spaces;
      self->n_words = state->n_words;

      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_CHARS]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_LINES]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_PRINTABLE]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_SPACES]);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_N_WORDS]);
    }

  return G_SOURCE_REMOVE;
}

static gpointer
editor_document_statistics_worker (gpointer data)
{
  g_autofree PangoLogAttr *attrs = NULL;
  Worker *state = data;
  LineReader reader;
  const char *line;
  gsize text_len;
  gsize len;
  gsize attrs_capacity = 0;
  gsize n_lines = 0;
  gsize n_chars = 0;
  gsize n_words = 0;
  gsize n_spaces = 0;

  g_assert (state != NULL);
  g_assert (state->text != NULL);
  g_assert (G_IS_CANCELLABLE (state->cancellable));

  text_len = strlen (state->text);
  n_chars = g_utf8_strlen (state->text, text_len);

  line_reader_init (&reader, state->text, text_len);

  while ((line = line_reader_next (&reader, &len)))
    {
      n_lines++;

      if (line[len] == '\n' || line[len] == '\r')
        n_spaces++;

      if (n_lines > 100 && g_cancellable_is_cancelled (state->cancellable))
        break;

      if (len == 0)
        continue;

      if (len > attrs_capacity)
        {
          g_free (attrs);

          attrs = g_new0 (PangoLogAttr, len + 1);
          attrs_capacity = len;
        }

      pango_get_log_attrs (line, len, 0, state->language, attrs, len + 1);

      for (gsize i = 0; i < len; i++)
        {
          if (attrs[i].is_white)
            n_spaces++;

          if (attrs[i].is_word_start)
            n_words++;
        }
    }

  state->n_chars = n_chars;
  state->n_lines = n_lines;
  state->n_spaces = n_spaces;
  state->n_words = n_words;
  state->n_printable = n_chars - n_spaces;

  g_idle_add_full (G_PRIORITY_DEFAULT,
                   editor_document_statistics_apply,
                   state,
                   (GDestroyNotify) worker_free);

  return NULL;
}

void
editor_document_statistics_reload (EditorDocumentStatistics *self)
{
  g_autoptr(EditorDocument) document = NULL;
  Worker *state;
  GtkTextIter begin;
  GtkTextIter end;

  g_assert (EDITOR_IS_DOCUMENT_STATISTICS (self));

  g_clear_handle_id (&self->queued_reload, g_source_remove);

  editor_document_statistics_cancel (self);

  if (!(document = editor_document_statistics_dup_document (self)))
    return;

  gtk_text_buffer_get_bounds (GTK_TEXT_BUFFER (document), &begin, &end);

  state = g_new0 (Worker, 1);
  g_weak_ref_set (&state->statistics_wr, self);
  state->text = gtk_text_iter_get_slice (&begin, &end);
  state->cancellable = g_object_ref (self->cancellable);
  state->language = gtk_text_iter_get_language (&begin);

  g_thread_unref (g_thread_new ("[editor-statistics]",
                                editor_document_statistics_worker,
                                state));
}

EditorDocument *
editor_document_statistics_dup_document (EditorDocumentStatistics *self)
{
  g_return_val_if_fail (EDITOR_IS_DOCUMENT_STATISTICS (self), NULL);

  return g_weak_ref_get (&self->document_wr);
}

static gboolean
editor_document_statistics_queue_reload_timeout (gpointer data)
{
  EditorDocumentStatistics *self = data;

  g_assert (EDITOR_IS_DOCUMENT_STATISTICS (self));

  self->queued_reload = 0;

  editor_document_statistics_reload (self);

  return G_SOURCE_REMOVE;
}

void
editor_document_statistics_queue_reload (EditorDocumentStatistics *self)
{
  g_return_if_fail (EDITOR_IS_DOCUMENT_STATISTICS (self));

  if (self->queued_reload)
    return;

  self->queued_reload = g_timeout_add (QUEUED_RELOAD_DELAY,
                                       editor_document_statistics_queue_reload_timeout,
                                       self);
}

EditorDocumentStatistics *
editor_document_statistics_new (EditorDocument *document)
{
  g_return_val_if_fail (!document || EDITOR_IS_DOCUMENT (document), NULL);

  return g_object_new (EDITOR_TYPE_DOCUMENT_STATISTICS,
                       "document", document,
                       NULL);
}
