/* editor-text-buffer-spell-adapter.c
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
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

#include "cjhtextregionprivate.h"

#include "editor-buffer-scheduler.h"
#include "editor-document.h"
#include "editor-spell-checker.h"
#include "editor-spell-cursor.h"
#include "editor-spell-language.h"
#include "editor-text-buffer-spell-adapter.h"

#define RUN_UNCHECKED      GSIZE_TO_POINTER(0)
#define RUN_CHECKED        GSIZE_TO_POINTER(1)
#define UPDATE_DELAY_MSECS 100
#define UPDATE_QUANTA_USEC (G_USEC_PER_SEC/1000L*2) /* 2 msec */

typedef struct
{
  gint64   deadline;
  guint    has_unchecked : 1;
} Update;

typedef struct
{
  gsize offset;
  guint found : 1;
} ScanForUnchecked;

struct _EditorTextBufferSpellAdapter
{
  GObject             parent_instance;

  GtkTextBuffer      *buffer;
  EditorSpellChecker *checker;
  CjhTextRegion      *region;
  GtkTextTag         *tag;
  GtkTextTag         *no_spell_check_tag;

  guint               cursor_position;

  gsize               update_source;

  guint               enabled : 1;
};

G_DEFINE_TYPE (EditorTextBufferSpellAdapter, editor_text_buffer_spell_adapter, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_BUFFER,
  PROP_CHECKER,
  PROP_ENABLED,
  PROP_LANGUAGE,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

EditorTextBufferSpellAdapter *
editor_text_buffer_spell_adapter_new (GtkTextBuffer      *buffer,
                                      EditorSpellChecker *checker)
{
  g_return_val_if_fail (GTK_IS_TEXT_BUFFER (buffer), NULL);
  g_return_val_if_fail (!checker || EDITOR_IS_SPELL_CHECKER (checker), NULL);

  return g_object_new (EDITOR_TYPE_TEXT_BUFFER_SPELL_ADAPTER,
                       "buffer", buffer,
                       "checker", checker,
                       NULL);
}

static inline gboolean
contains_iter (const GtkTextIter *begin,
               const GtkTextIter *end,
               const GtkTextIter *iter)
{
  return gtk_text_iter_compare (begin, iter) <= 0 &&
         gtk_text_iter_compare (iter, end) <= 0;
}

static gboolean
get_unchecked_start_cb (gsize                   offset,
                        const CjhTextRegionRun *run,
                        gpointer                user_data)
{
  gsize *pos = user_data;

  if (run->data == RUN_UNCHECKED)
    {
      *pos = offset;
      return TRUE;
    }

  return FALSE;
}

static gboolean
get_unchecked_start (CjhTextRegion *region,
                     GtkTextBuffer *buffer,
                     GtkTextIter   *iter)
{
  gsize pos = G_MAXSIZE;
  _cjh_text_region_foreach (region, get_unchecked_start_cb, &pos);
  if (pos == G_MAXSIZE)
    return FALSE;
  gtk_text_buffer_get_iter_at_offset (buffer, iter, pos);
  return TRUE;
}

static gboolean
editor_text_buffer_spell_adapter_update_range (EditorTextBufferSpellAdapter *self,
                                               gint64                        deadline)
{
  g_autoptr(EditorSpellCursor) cursor = NULL;
  GtkTextMark *mark;
  GtkTextIter insert, word_begin, word_end, last_match, begin;
  gboolean ret = FALSE;
  guint checked = 0;

  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));

  /* Ignore while we are loading or saving */
  if (editor_document_get_busy (EDITOR_DOCUMENT (self->buffer)))
    return TRUE;

  cursor = editor_spell_cursor_new (self->buffer, self->region, self->no_spell_check_tag);
  mark = gtk_text_buffer_get_insert (self->buffer);
  gtk_text_buffer_get_iter_at_mark (self->buffer, &insert, mark);

  /* Get the first unchecked position so that we can remove the tag
   * from it up to the first word match.
   */
  if (!get_unchecked_start (self->region, self->buffer, &begin))
    {
      _cjh_text_region_replace (self->region,
                                0,
                                _cjh_text_region_get_length (self->region),
                                RUN_CHECKED);
      return FALSE;
    }

  last_match = begin;
  while (editor_spell_cursor_next (cursor, &word_begin, &word_end))
    {
      g_autofree char *word = gtk_text_iter_get_slice (&word_begin, &word_end);

      checked++;

      if (!contains_iter (&word_begin, &word_end, &insert) &&
          !editor_spell_checker_check_word (self->checker, word, -1))
        {
          gtk_text_buffer_remove_tag (self->buffer, self->tag, &last_match, &word_end);
          gtk_text_buffer_apply_tag (self->buffer, self->tag, &word_begin, &word_end);
          last_match = word_end;
        }

      /* Check deadline every five words */
      if (checked % 5 == 0 && deadline < g_get_monotonic_time ())
        {
          ret = TRUE;
          break;
        }
    }

  /* Now remove from the last match to the end position */
  if (!gtk_text_iter_equal (&word_end, &last_match))
    gtk_text_buffer_remove_tag (self->buffer, self->tag, &last_match, &word_end);

  _cjh_text_region_replace (self->region,
                            gtk_text_iter_get_offset (&begin),
                            gtk_text_iter_get_offset (&word_end) - gtk_text_iter_get_offset (&begin),
                            RUN_CHECKED);

  return ret;
}

static gboolean
editor_text_buffer_spell_adapter_run (gint64   deadline,
                                      gpointer user_data)
{
  EditorTextBufferSpellAdapter *self = user_data;

  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));

  if (!editor_text_buffer_spell_adapter_update_range (self, deadline))
    {
      self->update_source = 0;
      return G_SOURCE_REMOVE;
    }

  return G_SOURCE_CONTINUE;
}

static void
editor_text_buffer_spell_adapter_queue_update (EditorTextBufferSpellAdapter *self)
{
  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));

  if (self->checker == NULL || self->buffer == NULL || !self->enabled)
    {
      editor_buffer_scheduler_clear (&self->update_source);
      return;
    }

  if (self->update_source == 0)
    editor_buffer_scheduler_add (editor_text_buffer_spell_adapter_run, self);
}

static void
editor_text_buffer_spell_adapter_invalidate_all (EditorTextBufferSpellAdapter *self)
{
  gsize length;

  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));

  length = _cjh_text_region_get_length (self->region);

  if (length > 0)
    {
      _cjh_text_region_replace (self->region, 0, length - 1, RUN_UNCHECKED);
      editor_text_buffer_spell_adapter_queue_update (self);
    }
}

static void
on_tag_added_cb (EditorTextBufferSpellAdapter *self,
                 GtkTextTag                   *tag,
                 GtkTextTagTable              *tag_table)
{
  char *name;

  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));
  g_assert (GTK_IS_TEXT_TAG (tag));
  g_assert (GTK_IS_TEXT_TAG_TABLE (tag_table));

  g_object_get (tag, "name", &name, NULL);
  if (name && strcmp (name, "gtksourceview:context-classes:no-spell-check") == 0)
    {
      g_set_object (&self->no_spell_check_tag, tag);
      editor_text_buffer_spell_adapter_invalidate_all (self);
    }
}

static void
on_tag_removed_cb (EditorTextBufferSpellAdapter *self,
                   GtkTextTag                   *tag,
                   GtkTextTagTable              *tag_table)
{
  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));
  g_assert (GTK_IS_TEXT_TAG (tag));
  g_assert (GTK_IS_TEXT_TAG_TABLE (tag_table));

  if (tag == self->no_spell_check_tag)
    {
      g_clear_object (&self->no_spell_check_tag);
      editor_text_buffer_spell_adapter_invalidate_all (self);
    }
}

static void
invalidate_tag_region_cb (EditorTextBufferSpellAdapter *self,
                          GtkTextTag                   *tag,
                          GtkTextIter                  *begin,
                          GtkTextIter                  *end,
                          GtkTextBuffer                *buffer)
{
  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));
  g_assert (GTK_IS_TEXT_TAG (tag));
  g_assert (GTK_IS_TEXT_BUFFER (buffer));

  if (tag == self->no_spell_check_tag)
    {
      gsize begin_offset = gtk_text_iter_get_offset (begin);
      gsize end_offset = gtk_text_iter_get_offset (end);

      _cjh_text_region_replace (self->region, begin_offset, end_offset - begin_offset, RUN_UNCHECKED);
      editor_text_buffer_spell_adapter_queue_update (self);
    }
}

static void
editor_text_buffer_spell_adapter_set_buffer (EditorTextBufferSpellAdapter *self,
                                             GtkTextBuffer                *buffer)
{
  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));
  g_assert (GTK_IS_TEXT_BUFFER (buffer));

  if (g_set_weak_pointer (&self->buffer, buffer))
    {
      GtkTextIter begin, end;
      GtkTextTagTable *tag_table;
      guint offset;
      guint length;

      gtk_text_buffer_get_bounds (buffer, &begin, &end);

      offset = gtk_text_iter_get_offset (&begin);
      length = gtk_text_iter_get_offset (&end) - offset;

      _cjh_text_region_insert (self->region, offset, length, RUN_UNCHECKED);

      self->tag = gtk_text_buffer_create_tag (buffer, NULL,
                                              "underline", PANGO_UNDERLINE_ERROR,
                                              NULL);

      /* Track tag changes from the tag table and extract "no-spell-check"
       * tag from GtkSourceView so that we can avoid words with that tag.
       */
      tag_table = gtk_text_buffer_get_tag_table (buffer);
      g_signal_connect_object (tag_table,
                               "tag-added",
                               G_CALLBACK (on_tag_added_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (tag_table,
                               "tag-removed",
                               G_CALLBACK (on_tag_removed_cb),
                               self,
                               G_CONNECT_SWAPPED);

      g_signal_connect_object (buffer,
                               "apply-tag",
                               G_CALLBACK (invalidate_tag_region_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (buffer,
                               "remove-tag",
                               G_CALLBACK (invalidate_tag_region_cb),
                               self,
                               G_CONNECT_SWAPPED);

      editor_text_buffer_spell_adapter_queue_update (self);
    }
}

static void
editor_text_buffer_spell_adapter_set_enabled (EditorTextBufferSpellAdapter *self,
                                              gboolean                      enabled)
{
  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));

  enabled = !!enabled;

  if (self->enabled != enabled)
    {
      GtkTextIter begin, end;

      self->enabled = enabled;

      if (self->buffer && self->tag && !self->enabled)
        {
          gtk_text_buffer_get_bounds (self->buffer, &begin, &end);
          gtk_text_buffer_remove_tag (self->buffer, self->tag, &begin, &end);
        }

      editor_text_buffer_spell_adapter_invalidate_all (self);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ENABLED]);
    }
}

static void
editor_text_buffer_spell_adapter_finalize (GObject *object)
{
  EditorTextBufferSpellAdapter *self = (EditorTextBufferSpellAdapter *)object;

  g_clear_object (&self->checker);
  g_clear_object (&self->no_spell_check_tag);
  g_clear_pointer (&self->region, _cjh_text_region_free);

  G_OBJECT_CLASS (editor_text_buffer_spell_adapter_parent_class)->finalize (object);
}

static void
editor_text_buffer_spell_adapter_dispose (GObject *object)
{
  EditorTextBufferSpellAdapter *self = (EditorTextBufferSpellAdapter *)object;

  g_clear_weak_pointer (&self->buffer);
  editor_buffer_scheduler_clear (&self->update_source);

  G_OBJECT_CLASS (editor_text_buffer_spell_adapter_parent_class)->dispose (object);
}

static void
editor_text_buffer_spell_adapter_get_property (GObject    *object,
                                               guint       prop_id,
                                               GValue     *value,
                                               GParamSpec *pspec)
{
  EditorTextBufferSpellAdapter *self = EDITOR_TEXT_BUFFER_SPELL_ADAPTER (object);

  switch (prop_id)
    {
    case PROP_BUFFER:
      g_value_set_object (value, self->buffer);
      break;

    case PROP_CHECKER:
      g_value_set_object (value, editor_text_buffer_spell_adapter_get_checker (self));
      break;

    case PROP_ENABLED:
      g_value_set_boolean (value, self->enabled);
      break;

    case PROP_LANGUAGE:
      g_value_set_string (value, editor_text_buffer_spell_adapter_get_language (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_text_buffer_spell_adapter_set_property (GObject      *object,
                                               guint         prop_id,
                                               const GValue *value,
                                               GParamSpec   *pspec)
{
  EditorTextBufferSpellAdapter *self = EDITOR_TEXT_BUFFER_SPELL_ADAPTER (object);

  switch (prop_id)
    {
    case PROP_BUFFER:
      editor_text_buffer_spell_adapter_set_buffer (self, g_value_get_object (value));
      break;

    case PROP_CHECKER:
      editor_text_buffer_spell_adapter_set_checker (self, g_value_get_object (value));
      break;

    case PROP_ENABLED:
      editor_text_buffer_spell_adapter_set_enabled (self, g_value_get_boolean (value));
      break;

    case PROP_LANGUAGE:
      editor_text_buffer_spell_adapter_set_language (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_text_buffer_spell_adapter_class_init (EditorTextBufferSpellAdapterClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_text_buffer_spell_adapter_dispose;
  object_class->finalize = editor_text_buffer_spell_adapter_finalize;
  object_class->get_property = editor_text_buffer_spell_adapter_get_property;
  object_class->set_property = editor_text_buffer_spell_adapter_set_property;

  properties [PROP_BUFFER] =
    g_param_spec_object ("buffer",
                         "Buffer",
                         "Buffer",
                         GTK_TYPE_TEXT_BUFFER,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_CHECKER] =
    g_param_spec_object ("checker",
                         "Checker",
                         "Checker",
                         EDITOR_TYPE_SPELL_CHECKER,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_ENABLED] =
    g_param_spec_boolean ("enabled",
                          "Enabled",
                          "If spellcheck is enabled",
                          TRUE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_LANGUAGE] =
    g_param_spec_string ("language",
                         "Language",
                         "The language code such as en_US",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_text_buffer_spell_adapter_init (EditorTextBufferSpellAdapter *self)
{
  self->region = _cjh_text_region_new (NULL, NULL);
  self->enabled = TRUE;
}

EditorSpellChecker *
editor_text_buffer_spell_adapter_get_checker (EditorTextBufferSpellAdapter *self)
{
  g_return_val_if_fail (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self), NULL);

  return self->checker;
}

void
editor_text_buffer_spell_adapter_set_checker (EditorTextBufferSpellAdapter *self,
                                              EditorSpellChecker           *checker)
{
  g_return_if_fail (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));
  g_return_if_fail (!checker || EDITOR_IS_SPELL_CHECKER (checker));

  if (g_set_object (&self->checker, checker))
    {
      gsize length = _cjh_text_region_get_length (self->region);

      editor_buffer_scheduler_clear (&self->update_source);

      if (length > 0)
        {
          _cjh_text_region_remove (self->region, 0, length - 1);
          _cjh_text_region_insert (self->region, 0, length, RUN_UNCHECKED);
          g_assert_cmpint (length, ==, _cjh_text_region_get_length (self->region));
        }

      editor_text_buffer_spell_adapter_queue_update (self);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CHECKER]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_LANGUAGE]);
    }
}

GtkTextBuffer *
editor_text_buffer_spell_adapter_get_buffer (EditorTextBufferSpellAdapter *self)
{
  g_return_val_if_fail (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self), NULL);

  return self->buffer;
}

void
editor_text_buffer_spell_adapter_insert_text (EditorTextBufferSpellAdapter *self,
                                              guint                         offset,
                                              guint                         length)
{
  g_return_if_fail (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));
  g_return_if_fail (length > 0);

  _cjh_text_region_insert (self->region, offset, length, RUN_UNCHECKED);

  editor_text_buffer_spell_adapter_queue_update (self);
}

static void
invalidate_surrounding (EditorTextBufferSpellAdapter *self,
                        guint                         offset)
{
  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));

  if (offset)
    _cjh_text_region_replace (self->region, offset - 1, 1, RUN_UNCHECKED);

  if (offset + 1 < _cjh_text_region_get_length (self->region))
    _cjh_text_region_replace (self->region, offset, 1, RUN_UNCHECKED);

  editor_text_buffer_spell_adapter_queue_update (self);
}

void
editor_text_buffer_spell_adapter_delete_range (EditorTextBufferSpellAdapter *self,
                                               guint                         offset,
                                               guint                         length)
{
  g_return_if_fail (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));
  g_return_if_fail (self->buffer != NULL);
  g_return_if_fail (length > 0);

  _cjh_text_region_remove (self->region, offset, length);
  invalidate_surrounding (self, offset);
}

void
editor_text_buffer_spell_adapter_cursor_moved (EditorTextBufferSpellAdapter *self,
                                               guint                         position)
{
  g_return_if_fail (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));
  g_return_if_fail (self->buffer != NULL);

  invalidate_surrounding (self, self->cursor_position);
  self->cursor_position = position;
}

const char *
editor_text_buffer_spell_adapter_get_language (EditorTextBufferSpellAdapter *self)
{
  g_return_val_if_fail (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self), NULL);

  return self->checker ? editor_spell_checker_get_language (self->checker) : NULL;
}

void
editor_text_buffer_spell_adapter_set_language (EditorTextBufferSpellAdapter *self,
                                               const char                   *language)
{
  g_return_if_fail (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));

  if (self->checker == NULL && language == NULL)
    return;

  if (self->checker == NULL)
    {
      self->checker = editor_spell_checker_new (NULL, language);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CHECKER]);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_LANGUAGE]);
    }
  else if (g_strcmp0 (language, editor_text_buffer_spell_adapter_get_language (self)) != 0)
    {
      editor_spell_checker_set_language (self->checker, language);
      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_LANGUAGE]);
    }

  editor_text_buffer_spell_adapter_invalidate_all (self);
}
