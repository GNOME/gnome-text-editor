/*
 * editor-source-buffer.c
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

#include "editor-source-buffer.h"

struct _EditorSourceBuffer
{
  GtkSourceBuffer parent_instance;
};

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorSourceBuffer, editor_source_buffer, GTK_SOURCE_TYPE_BUFFER)

static GParamSpec *properties[N_PROPS];

static void
editor_source_buffer_finalize (GObject *object)
{
  EditorSourceBuffer *self = (EditorSourceBuffer *)object;

  G_OBJECT_CLASS (editor_source_buffer_parent_class)->finalize (object);
}

static void
editor_source_buffer_get_property (GObject    *object,
                                   guint       prop_id,
                                   GValue     *value,
                                   GParamSpec *pspec)
{
  EditorSourceBuffer *self = EDITOR_SOURCE_BUFFER (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_source_buffer_set_property (GObject      *object,
                                   guint         prop_id,
                                   const GValue *value,
                                   GParamSpec   *pspec)
{
  EditorSourceBuffer *self = EDITOR_SOURCE_BUFFER (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_source_buffer_class_init (EditorSourceBufferClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = editor_source_buffer_finalize;
  object_class->get_property = editor_source_buffer_get_property;
  object_class->set_property = editor_source_buffer_set_property;
}

static void
editor_source_buffer_init (EditorSourceBuffer *self)
{
}

EditorSourceBuffer *
editor_source_buffer_new (void)
{
  return g_object_new (EDITOR_TYPE_SOURCE_BUFFER, NULL);
}
