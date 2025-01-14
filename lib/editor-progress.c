/*
 * editor-progress.c
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

#include "editor-progress.h"

struct _EditorProgress
{
  GObject parent_instance;
  char *message;
  double fraction;
};

enum {
  PROP_0,
  PROP_MESSAGE,
  PROP_FRACTION,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorProgress, editor_progress, G_TYPE_OBJECT)

static GParamSpec *properties[N_PROPS];

static void
editor_progress_finalize (GObject *object)
{
  EditorProgress *self = (EditorProgress *)object;

  g_clear_pointer (&self->message, g_free);

  G_OBJECT_CLASS (editor_progress_parent_class)->finalize (object);
}

static void
editor_progress_get_property (GObject    *object,
                              guint       prop_id,
                              GValue     *value,
                              GParamSpec *pspec)
{
  EditorProgress *self = EDITOR_PROGRESS (object);

  switch (prop_id)
    {
    case PROP_MESSAGE:
      g_value_take_string (value, editor_progress_dup_message (self));
      break;

    case PROP_FRACTION:
      g_value_set_double (value, editor_progress_get_fraction (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_progress_set_property (GObject      *object,
                              guint         prop_id,
                              const GValue *value,
                              GParamSpec   *pspec)
{
  EditorProgress *self = EDITOR_PROGRESS (object);

  switch (prop_id)
    {
    case PROP_MESSAGE:
      editor_progress_set_message (self, g_value_get_string (value));
      break;

    case PROP_FRACTION:
      editor_progress_set_fraction (self, g_value_get_double (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_progress_class_init (EditorProgressClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = editor_progress_finalize;
  object_class->get_property = editor_progress_get_property;
  object_class->set_property = editor_progress_set_property;

  properties[PROP_MESSAGE] =
    g_param_spec_string ("message", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_FRACTION] =
    g_param_spec_double ("fraction", NULL, NULL,
                         0, 1, 0,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_progress_init (EditorProgress *self)
{
}

EditorProgress *
editor_progress_new (void)
{
  return g_object_new (EDITOR_TYPE_PROGRESS, NULL);
}

void
editor_progress_set_message (EditorProgress *self,
                             const char     *message)
{
  g_return_if_fail (EDITOR_IS_PROGRESS (self));

  if (g_set_str (&self->message, message))
    g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MESSAGE]);
}

char *
editor_progress_dup_message (EditorProgress *self)
{
  g_return_val_if_fail (EDITOR_IS_PROGRESS (self), NULL);

  return g_strdup (self->message);
}

void
editor_progress_set_fraction (EditorProgress *self,
                              double          fraction)
{
  g_return_if_fail (EDITOR_IS_PROGRESS (self));

  fraction = CLAMP (fraction, 0., 1.);

  if (fraction != self->fraction)
    {
      self->fraction = fraction;
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_FRACTION]);
    }
}

double
editor_progress_get_fraction (EditorProgress *self)
{
  g_return_val_if_fail (EDITOR_IS_PROGRESS (self), 0);

  return self->fraction;
}

void
editor_progress_file_callback (goffset  current,
                               goffset  total,
                               gpointer user_data)
{
  g_return_if_fail (EDITOR_IS_PROGRESS (user_data));

  if (total == 0)
    editor_progress_set_fraction (user_data, 0);
  else
    editor_progress_set_fraction (user_data, (double)current / (double)total);
}
