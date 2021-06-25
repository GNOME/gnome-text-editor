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

#include "editor-spell-checker.h"
#include "editor-text-buffer-spell-adapter.h"

#define UNCHECKED          GSIZE_TO_POINTER(0)
#define CHECKED            GSIZE_TO_POINTER(1)
#define UPDATE_DELAY_MSECS 200

struct _EditorTextBufferSpellAdapter
{
  GObject             parent_instance;

  GtkTextBuffer      *buffer;
  EditorSpellChecker *checker;
  CjhTextRegion      *region;

  guint               cursor_position;

  guint               update_source;
};

G_DEFINE_TYPE (EditorTextBufferSpellAdapter, editor_text_buffer_spell_adapter, G_TYPE_OBJECT)

enum {
  PROP_0,
  PROP_BUFFER,
  PROP_CHECKER,
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

static gboolean
editor_text_buffer_spell_adapter_update_range (EditorTextBufferSpellAdapter *self,
                                               gsize                         begin,
                                               gsize                         end,
                                               gint64                        deadline)
{
  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));

  return FALSE;
}

static gboolean
editor_text_buffer_spell_adapter_update (EditorTextBufferSpellAdapter *self)
{
  gint64 deadline;
  gboolean has_more;
  gsize length;

  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));

  deadline = g_get_monotonic_time () + (G_USEC_PER_SEC/1000L);
  length = _cjh_text_region_get_length (self->region);
  has_more = editor_text_buffer_spell_adapter_update_range (self, 0, length, deadline);

  if (has_more)
    return G_SOURCE_CONTINUE;

  self->update_source = 0;

  return G_SOURCE_REMOVE;
}

static void
editor_text_buffer_spell_adapter_queue_update (EditorTextBufferSpellAdapter *self)
{
  g_assert (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));

  if (self->checker == NULL)
    return;

  if (self->update_source == 0)
    self->update_source = g_timeout_add_full (G_PRIORITY_LOW,
                                              UPDATE_DELAY_MSECS,
                                              (GSourceFunc) editor_text_buffer_spell_adapter_update,
                                              g_object_ref (self),
                                              g_object_unref);
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
      guint offset;
      guint length;

      gtk_text_buffer_get_bounds (buffer, &begin, &end);

      offset = gtk_text_iter_get_offset (&begin);
      length = gtk_text_iter_get_offset (&end) - offset;

      _cjh_text_region_insert (self->region, offset, length, UNCHECKED);
    }
}

static void
editor_text_buffer_spell_adapter_finalize (GObject *object)
{
  EditorTextBufferSpellAdapter *self = (EditorTextBufferSpellAdapter *)object;

  g_clear_object (&self->checker);
  g_clear_pointer (&self->region, _cjh_text_region_free);

  G_OBJECT_CLASS (editor_text_buffer_spell_adapter_parent_class)->finalize (object);
}

static void
editor_text_buffer_spell_adapter_dispose (GObject *object)
{
  EditorTextBufferSpellAdapter *self = (EditorTextBufferSpellAdapter *)object;

  g_clear_weak_pointer (&self->buffer);
  g_clear_handle_id (&self->update_source, g_source_remove);

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

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_text_buffer_spell_adapter_init (EditorTextBufferSpellAdapter *self)
{
  self->region = _cjh_text_region_new (NULL, NULL);
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

      g_clear_handle_id (&self->update_source, g_source_remove);

      if (length > 0)
        {
          _cjh_text_region_remove (self->region, 0, length - 1);
          _cjh_text_region_insert (self->region, 0, length, UNCHECKED);
          g_assert_cmpint (length, ==, _cjh_text_region_get_length (self->region));
        }

      editor_text_buffer_spell_adapter_queue_update (self);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_CHECKER]);
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

  _cjh_text_region_insert (self->region, offset, length, UNCHECKED);
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
}

void
editor_text_buffer_spell_adapter_cursor_moved (EditorTextBufferSpellAdapter *self,
                                               guint                         position)
{
  g_return_if_fail (EDITOR_IS_TEXT_BUFFER_SPELL_ADAPTER (self));
  g_return_if_fail (self->buffer != NULL);

  self->cursor_position = position;

  editor_text_buffer_spell_adapter_queue_update (self);
}
