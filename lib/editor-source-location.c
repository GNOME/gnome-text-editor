/*
 * editor-source-location.c
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

#include <stdio.h>

#include "editor-source-location.h"

struct _EditorSourceLocation
{
  GObject parent_instance;
  guint line;
  guint line_offset;
} EditorSourceLocationPrivate;

enum {
  PROP_0,
  PROP_LINE,
  PROP_LINE_OFFSET,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorSourceLocation, editor_source_location, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static void
editor_source_location_get_property (GObject    *object,
                                     guint       prop_id,
                                     GValue     *value,
                                     GParamSpec *pspec)
{
  EditorSourceLocation *self = EDITOR_SOURCE_LOCATION (object);

  switch (prop_id)
    {
    case PROP_LINE:
      g_value_set_uint (value, self->line);
      break;

    case PROP_LINE_OFFSET:
      g_value_set_uint (value, self->line_offset);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_source_location_set_property (GObject      *object,
                                     guint         prop_id,
                                     const GValue *value,
                                     GParamSpec   *pspec)
{
  EditorSourceLocation *self = EDITOR_SOURCE_LOCATION (object);

  switch (prop_id)
    {
    case PROP_LINE:
      self->line = g_value_get_uint (value);
      break;

    case PROP_LINE_OFFSET:
      self->line_offset = g_value_get_uint (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_source_location_class_init (EditorSourceLocationClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->get_property = editor_source_location_get_property;
  object_class->set_property = editor_source_location_set_property;

  properties[PROP_LINE] =
    g_param_spec_uint ("line", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS));

  properties[PROP_LINE_OFFSET] =
    g_param_spec_uint ("line-offset", NULL, NULL,
                       0, G_MAXUINT, 0,
                       (G_PARAM_READWRITE |
                        G_PARAM_CONSTRUCT_ONLY |
                        G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_source_location_init (EditorSourceLocation *self)
{
}

EditorSourceLocation *
editor_source_location_new (guint line,
                            guint line_offset)
{
  return g_object_new (EDITOR_TYPE_SOURCE_LOCATION,
                       "line", line,
                       "line-offset", line_offset,
                       NULL);
}

guint
editor_source_location_get_line (EditorSourceLocation *self)
{
  g_return_val_if_fail (EDITOR_IS_SOURCE_LOCATION (self), 0);

  return self->line;
}

guint
editor_source_location_get_line_offset (EditorSourceLocation *self)
{
  g_return_val_if_fail (EDITOR_IS_SOURCE_LOCATION (self), 0);

  return self->line_offset;
}

char *
editor_source_location_to_string (EditorSourceLocation *self)
{
  g_return_val_if_fail (EDITOR_IS_SOURCE_LOCATION (self), NULL);

  return g_strdup_printf ("%u:%u", self->line, self->line_offset);
}

EditorSourceLocation *
editor_source_location_new_from_string (const char *string)
{
  guint line = 0;
  guint line_offset = 0;

  g_return_val_if_fail (string != NULL, NULL);

  if (sscanf (string, "%u:%u", &line, &line_offset) >= 1)
    return editor_source_location_new (line, line_offset);

  return NULL;
}
