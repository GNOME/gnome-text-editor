/* editor-language-row.c
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

#define G_LOG_DOMAIN "editor-language-row"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-language-row-private.h"

struct _EditorLanguageRow
{
  AdwActionRow       parent_instance;

  GtkSourceLanguage *language;
  char              *id;
  char              *name;

  GtkImage          *image;
};

enum {
  PROP_0,
  PROP_LANGUAGE,
  N_PROPS
};

G_DEFINE_TYPE (EditorLanguageRow, editor_language_row, ADW_TYPE_ACTION_ROW)

static GParamSpec *properties [N_PROPS];

static void
editor_language_row_constructed (GObject *object)
{
  EditorLanguageRow *self = (EditorLanguageRow *)object;
  const char *name;

  g_assert (EDITOR_IS_LANGUAGE_ROW (self));

  G_OBJECT_CLASS (editor_language_row_parent_class)->constructed (object);

  if (self->language == NULL)
    name = _("Plain Text");
  else
    name = gtk_source_language_get_name (self->language);

  adw_preferences_row_set_title (ADW_PREFERENCES_ROW (self), name);
}

static void
editor_language_row_finalize (GObject *object)
{
  EditorLanguageRow *self = (EditorLanguageRow *)object;

  g_clear_object (&self->language);
  g_clear_pointer (&self->id, g_free);
  g_clear_pointer (&self->name, g_free);

  G_OBJECT_CLASS (editor_language_row_parent_class)->finalize (object);
}

static void
editor_language_row_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EditorLanguageRow *self = EDITOR_LANGUAGE_ROW (object);

  switch (prop_id)
    {
    case PROP_LANGUAGE:
      g_value_set_object (value, self->language);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_language_row_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EditorLanguageRow *self = EDITOR_LANGUAGE_ROW (object);

  switch (prop_id)
    {
    case PROP_LANGUAGE:
      if (g_value_get_object (value))
        {
          self->language = g_value_dup_object (value);
          self->id = g_utf8_strdown (gtk_source_language_get_id (self->language), -1);
          self->name = g_utf8_strdown (gtk_source_language_get_name (self->language), -1);
        }
      else
        {
          self->id = g_strdup ("plaintext");
          self->name = g_utf8_strdown (_("Plain Text"), -1);
        }

      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_language_row_class_init (EditorLanguageRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->constructed = editor_language_row_constructed;
  object_class->finalize = editor_language_row_finalize;
  object_class->get_property = editor_language_row_get_property;
  object_class->set_property = editor_language_row_set_property;

  properties [PROP_LANGUAGE] =
    g_param_spec_object ("language",
                         "Language",
                         "The language for the row",
                         GTK_SOURCE_TYPE_LANGUAGE,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-language-row.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorLanguageRow, image);
}

static void
editor_language_row_init (EditorLanguageRow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
_editor_language_row_new (GtkSourceLanguage *language)
{
  g_return_val_if_fail (!language || GTK_SOURCE_IS_LANGUAGE (language), NULL);

  return g_object_new (EDITOR_TYPE_LANGUAGE_ROW,
                       "activatable", TRUE,
                       "language", language,
                       NULL);
}

void
_editor_language_row_set_selected (EditorLanguageRow *self,
                                   gboolean           selected)
{
  g_return_if_fail (EDITOR_IS_LANGUAGE_ROW (self));

  gtk_widget_set_visible (GTK_WIDGET (self->image), selected);
}

GtkSourceLanguage *
_editor_language_row_get_language (EditorLanguageRow *self)
{
  g_return_val_if_fail (EDITOR_IS_LANGUAGE_ROW (self), NULL);

  return self->language;
}

gboolean
_editor_language_row_match (EditorLanguageRow *self,
                            GPatternSpec      *spec)
{
  g_return_val_if_fail (EDITOR_IS_LANGUAGE_ROW (self), FALSE);

  if (spec == NULL)
    return TRUE;

  return g_pattern_spec_match_string (spec, self->id) ||
         g_pattern_spec_match_string (spec, self->name);
}
