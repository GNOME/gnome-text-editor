/* editor-preferences-radio.c
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

#define G_LOG_DOMAIN "editor-preferences-radio"

#include "config.h"

#include "editor-preferences-radio.h"

struct _EditorPreferencesRadio
{
  AdwActionRow  row;

  GtkCheckButton       *toggle;

  GSettings            *settings;
  gchar                *schema_id;
  gchar                *schema_key;
  gchar                *schema_value;
};

enum {
  PROP_0,
  PROP_SCHEMA_ID,
  PROP_SCHEMA_KEY,
  PROP_SCHEMA_VALUE,
  N_PROPS
};

G_DEFINE_TYPE (EditorPreferencesRadio, editor_preferences_radio, ADW_TYPE_ACTION_ROW)

static GParamSpec *properties [N_PROPS];

static void
editor_preferences_radio_changed_cb (EditorPreferencesRadio *self,
                                     const gchar            *key,
                                     GSettings              *settings)
{
  g_autofree gchar *value = NULL;
  gboolean active;

  g_assert (EDITOR_IS_PREFERENCES_RADIO (self));
  g_assert (key != NULL);
  g_assert (G_IS_SETTINGS (settings));

  value = g_settings_get_string (settings, key);
  active = g_strcmp0 (value, self->schema_value) == 0;
  gtk_check_button_set_active (self->toggle, active);
}

static void
editor_preferences_radio_constructed (GObject *object)
{
  EditorPreferencesRadio *self = (EditorPreferencesRadio *)object;
  g_autofree gchar *changed = NULL;

  G_OBJECT_CLASS (editor_preferences_radio_parent_class)->constructed (object);

  if (self->schema_id == NULL || self->schema_key == NULL || self->schema_value == NULL)
    {
      g_warning ("Cannot setup preferences switch, missing schema properties");
      return;
    }

  self->settings = g_settings_new (self->schema_id);
  changed = g_strdup_printf ("changed::%s", self->schema_key);

  g_signal_connect_object (self->settings,
                           changed,
                           G_CALLBACK (editor_preferences_radio_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (object), TRUE);

  editor_preferences_radio_changed_cb (self, self->schema_key, self->settings);
}

static void
editor_preferences_radio_activated (AdwActionRow *row)
{
  EditorPreferencesRadio *self = (EditorPreferencesRadio *)row;

  g_assert (ADW_IS_ACTION_ROW (self));

  g_settings_set_string (self->settings, self->schema_key, self->schema_value);
}

static void
editor_preferences_radio_finalize (GObject *object)
{
  EditorPreferencesRadio *self = (EditorPreferencesRadio *)object;

  g_clear_object (&self->settings);
  g_clear_pointer (&self->schema_id, g_free);
  g_clear_pointer (&self->schema_key, g_free);
  g_clear_pointer (&self->schema_value, g_free);

  G_OBJECT_CLASS (editor_preferences_radio_parent_class)->finalize (object);
}

static void
editor_preferences_radio_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  EditorPreferencesRadio *self = EDITOR_PREFERENCES_RADIO (object);

  switch (prop_id)
    {
    case PROP_SCHEMA_ID:
      g_value_set_string (value, self->schema_id);
      break;

    case PROP_SCHEMA_KEY:
      g_value_set_string (value, self->schema_key);
      break;

    case PROP_SCHEMA_VALUE:
      g_value_set_string (value, self->schema_value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_preferences_radio_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  EditorPreferencesRadio *self = EDITOR_PREFERENCES_RADIO (object);

  switch (prop_id)
    {
    case PROP_SCHEMA_ID:
      self->schema_id = g_value_dup_string (value);
      break;

    case PROP_SCHEMA_KEY:
      self->schema_key = g_value_dup_string (value);
      break;

    case PROP_SCHEMA_VALUE:
      self->schema_value = g_value_dup_string (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_preferences_radio_class_init (EditorPreferencesRadioClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  AdwActionRowClass *row_class = ADW_ACTION_ROW_CLASS (klass);

  object_class->constructed = editor_preferences_radio_constructed;
  object_class->finalize = editor_preferences_radio_finalize;
  object_class->get_property = editor_preferences_radio_get_property;
  object_class->set_property = editor_preferences_radio_set_property;

  row_class->activate = editor_preferences_radio_activated;

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

  properties [PROP_SCHEMA_VALUE] =
    g_param_spec_string ("schema-value",
                         "Schema Value",
                         "The value for the key",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
editor_preferences_radio_init (EditorPreferencesRadio *self)
{
  self->toggle = g_object_new (GTK_TYPE_CHECK_BUTTON,
                               "can-focus", FALSE,
                               "valign", GTK_ALIGN_CENTER,
                               NULL);
  adw_action_row_add_prefix (ADW_ACTION_ROW (self), GTK_WIDGET (self->toggle));
}

void
editor_preferences_radio_set_group (EditorPreferencesRadio *self,
                                    EditorPreferencesRadio *other)
{
  g_return_if_fail (EDITOR_IS_PREFERENCES_RADIO (self));
  g_return_if_fail (EDITOR_IS_PREFERENCES_RADIO (other));

  gtk_check_button_set_group (self->toggle, other->toggle);
}
