/*
 * editor-encoding-row.c
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

#define G_LOG_DOMAIN "editor-encoding-row"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-encoding-row-private.h"

struct _EditorEncodingRow
{
  AdwActionRow       parent_instance;

  EditorEncoding    *encoding;
  char              *id;
  char              *name;

  GtkImage          *image;
};

enum {
  PROP_0,
  PROP_ENCODING,
  N_PROPS
};

G_DEFINE_TYPE (EditorEncodingRow, editor_encoding_row, ADW_TYPE_ACTION_ROW)

static GParamSpec *properties [N_PROPS];

static char *
take_strdown (char *s)
{
  g_autofree char *tmp = s;
  return g_utf8_strdown (tmp, -1);
}

static void
editor_encoding_row_constructed (GObject *object)
{
  EditorEncodingRow *self = (EditorEncodingRow *)object;
  const char *name;

  g_assert (EDITOR_IS_ENCODING_ROW (self));

  G_OBJECT_CLASS (editor_encoding_row_parent_class)->constructed (object);

  name = editor_encoding_dup_name (self->encoding);

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self), name);
}

static void
editor_encoding_row_finalize (GObject *object)
{
  EditorEncodingRow *self = (EditorEncodingRow *)object;

  g_clear_object (&self->encoding);
  g_clear_pointer (&self->id, g_free);
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (editor_encoding_row_parent_class)->finalize (object);
}

static void
editor_encoding_row_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EditorEncodingRow *self = EDITOR_ENCODING_ROW (object);

  switch (prop_id)
    {
    case PROP_ENCODING:
      g_value_set_object (value, self->encoding);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_encoding_row_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EditorEncodingRow *self = EDITOR_ENCODING_ROW (object);

  switch (prop_id)
    {
    case PROP_ENCODING:
      self->encoding = g_value_dup_object (value);
      self->id = take_strdown (editor_encoding_dup_charset (self->encoding));
      self->name = take_strdown (editor_encoding_dup_name (self->encoding));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_encoding_row_class_init (EditorEncodingRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = editor_encoding_row_constructed;
  object_class->finalize = editor_encoding_row_finalize;
  object_class->get_property = editor_encoding_row_get_property;
  object_class->set_property = editor_encoding_row_set_property;

  properties [PROP_ENCODING] =
    g_param_spec_object ("encoding", NULL, NULL,
                         EDITOR_TYPE_ENCODING,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-encoding-row.ui");

  gtk_widget_class_bind_template_child (widget_class, EditorEncodingRow, image);
}

static void
editor_encoding_row_init (EditorEncodingRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
_editor_encoding_row_new (EditorEncoding *encoding)
{
  g_return_val_if_fail (EDITOR_IS_ENCODING (encoding), NULL);

  return g_object_new (EDITOR_TYPE_ENCODING_ROW,
                       "activatable", TRUE,
                       "encoding", encoding,
                       NULL);
}

void
_editor_encoding_row_set_selected (EditorEncodingRow *self,
                                   gboolean           selected)
{
  g_return_if_fail (EDITOR_IS_ENCODING_ROW (self));

  gtk_widget_set_visible (GTK_WIDGET (self->image), selected);
}

EditorEncoding *
_editor_encoding_row_get_encoding (EditorEncodingRow *self)
{
  g_return_val_if_fail (EDITOR_IS_ENCODING_ROW (self), NULL);

  return self->encoding;
}

gboolean
_editor_encoding_row_match (EditorEncodingRow *self,
                            GPatternSpec      *spec)
{
  g_return_val_if_fail (EDITOR_IS_ENCODING_ROW (self), FALSE);

  if (spec == NULL)
    return TRUE;

  return g_pattern_spec_match_string (spec, self->id) ||
         g_pattern_spec_match_string (spec, self->name);
}

