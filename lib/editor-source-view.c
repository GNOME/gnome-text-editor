/*
 * editor-source-view.c
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
#include "editor-source-view.h"

struct _EditorSourceView
{
  GtkSourceView parent_instance;
};

enum {
  PROP_0,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorSourceView, editor_source_view, GTK_SOURCE_TYPE_VIEW)

static GParamSpec *properties[N_PROPS];

static void
editor_source_view_finalize (GObject *object)
{
  EditorSourceView *self = (EditorSourceView *)object;

  G_OBJECT_CLASS (editor_source_view_parent_class)->finalize (object);
}

static void
editor_source_view_get_property (GObject    *object,
                                 guint       prop_id,
                                 GValue     *value,
                                 GParamSpec *pspec)
{
  EditorSourceView *self = EDITOR_SOURCE_VIEW (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_source_view_set_property (GObject      *object,
                                 guint         prop_id,
                                 const GValue *value,
                                 GParamSpec   *pspec)
{
  EditorSourceView *self = EDITOR_SOURCE_VIEW (object);

  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_source_view_class_init (EditorSourceViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = editor_source_view_finalize;
  object_class->get_property = editor_source_view_get_property;
  object_class->set_property = editor_source_view_set_property;
}

static void
editor_source_view_init (EditorSourceView *self)
{
}

GtkWidget *
editor_source_view_new (EditorSourceBuffer *buffer)
{
  g_return_val_if_fail (EDITOR_IS_SOURCE_BUFFER (buffer), NULL);

  return g_object_new (EDITOR_TYPE_SOURCE_VIEW,
                       "buffer", buffer,
                       NULL);
}
