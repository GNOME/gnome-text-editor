/* editor-preferences-font.c
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

#define G_LOG_DOMAIN "editor-preferences-font"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-preferences-font.h"

struct _EditorPreferencesFont
{
  AdwActionRow  row;

  GSettings            *settings;
  gchar                *schema_id;
  gchar                *schema_key;
};

enum {
  PROP_0,
  PROP_SCHEMA_ID,
  PROP_SCHEMA_KEY,
  N_PROPS
};

G_DEFINE_TYPE (EditorPreferencesFont, editor_preferences_font, ADW_TYPE_ACTION_ROW)

static GParamSpec *properties [N_PROPS];

static void
editor_preferences_font_constructed (GObject *object)
{
  EditorPreferencesFont *self = (EditorPreferencesFont *)object;

  G_OBJECT_CLASS (editor_preferences_font_parent_class)->constructed (object);

  if (self->schema_id == NULL || self->schema_key == NULL)
    {
      g_warning ("Cannot setup preferences switch, missing schema properties");
      return;
    }

  self->settings = g_settings_new (self->schema_id);

  g_settings_bind (self->settings, self->schema_key,
                   self, "title",
                   G_SETTINGS_BIND_GET);

  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (object), TRUE);
}

static void
editor_preferences_font_dialog_choose_font_cb (GObject      *object,
                                               GAsyncResult *res,
                                               gpointer      user_data)
{
  GtkFontDialog *dialog = (GtkFontDialog *)object;
  g_autoptr(EditorPreferencesFont) self = user_data;
  PangoFontDescription* font_desc;

  g_assert (EDITOR_IS_PREFERENCES_FONT (self));
  g_assert (GTK_IS_FONT_DIALOG (dialog));

  if ((font_desc = gtk_font_dialog_choose_font_finish (dialog, res, NULL)))
    {
      g_autofree char *font = pango_font_description_to_string (font_desc);
      g_settings_set_string (self->settings, self->schema_key, font);
      g_clear_pointer (&font_desc, pango_font_description_free);
    }
}

static void
editor_preferences_font_activated (AdwActionRow *row)
{
  EditorPreferencesFont *self = (EditorPreferencesFont *)row;
  PangoFontDescription *font_desc;
  g_autofree char *font = NULL;
  GtkFontDialog *dialog;
  GtkWidget *window;

  g_assert (ADW_IS_ACTION_ROW (self));

  window = gtk_widget_get_ancestor (GTK_WIDGET (row), GTK_TYPE_WINDOW);
  dialog = gtk_font_dialog_new ();
  gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

  font = g_settings_get_string (self->settings, self->schema_key);
  font_desc = pango_font_description_from_string (font);

  gtk_font_dialog_choose_font (dialog,
                               GTK_WINDOW (window),
                               font_desc,
                               NULL,
                               editor_preferences_font_dialog_choose_font_cb,
                               g_object_ref (self));

  g_clear_pointer (&font_desc, pango_font_description_free);
}

static void
editor_preferences_font_finalize (GObject *object)
{
  EditorPreferencesFont *self = (EditorPreferencesFont *)object;

  g_clear_object (&self->settings);
  g_clear_pointer (&self->schema_id, g_free);
  g_clear_pointer (&self->schema_key, g_free);

  G_OBJECT_CLASS (editor_preferences_font_parent_class)->finalize (object);
}

static void
editor_preferences_font_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  EditorPreferencesFont *self = EDITOR_PREFERENCES_FONT (object);

  switch (prop_id)
    {
    case PROP_SCHEMA_ID:
      g_value_set_string (value, self->schema_id);
      break;

    case PROP_SCHEMA_KEY:
      g_value_set_string (value, self->schema_key);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_preferences_font_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  EditorPreferencesFont *self = EDITOR_PREFERENCES_FONT (object);

  switch (prop_id)
    {
    case PROP_SCHEMA_ID:
      self->schema_id = g_value_dup_string (value);
      break;

    case PROP_SCHEMA_KEY:
      self->schema_key = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_preferences_font_class_init (EditorPreferencesFontClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  AdwActionRowClass *row_class = ADW_ACTION_ROW_CLASS (klass);

  object_class->constructed = editor_preferences_font_constructed;
  object_class->finalize = editor_preferences_font_finalize;
  object_class->get_property = editor_preferences_font_get_property;
  object_class->set_property = editor_preferences_font_set_property;

  row_class->activate = editor_preferences_font_activated;

  properties [PROP_SCHEMA_ID] =
    g_param_spec_string ("schema-id",
                         "Schema Id",
                         "The identifier of the GSettings schema",
                         "org.gnome.TextEditor",
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  properties [PROP_SCHEMA_KEY] =
    g_param_spec_string ("schema-key",
                         "Schema Key",
                         "The key within the GSettings schema",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_preferences_font_init (EditorPreferencesFont *self)
{
  adw_action_row_add_suffix (ADW_ACTION_ROW (self),
                             gtk_image_new_from_icon_name ("go-next-symbolic"));
}
