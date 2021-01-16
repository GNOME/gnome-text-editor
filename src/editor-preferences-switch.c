/* editor-preferences-switch.c
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

#define G_LOG_DOMAIN "editor-preferences-switch"

#include "config.h"

#include "editor-preferences-switch.h"

struct _EditorPreferencesSwitch
{
  AdwActionRow  row;

  GtkLabel             *label;
  GtkSwitch            *toggle;

  GSettings            *settings;
  gchar                *schema_id;
  gchar                *schema_key;
};

enum {
  PROP_0,
  PROP_ACTIVE,
  PROP_LABEL,
  PROP_SCHEMA_ID,
  PROP_SCHEMA_KEY,
  N_PROPS
};

G_DEFINE_TYPE (EditorPreferencesSwitch, editor_preferences_switch, ADW_TYPE_ACTION_ROW)

static GParamSpec *properties [N_PROPS];

static void
editor_preferences_switch_notify_active_cb (EditorPreferencesSwitch *self,
                                            GParamSpec              *pspec,
                                            GtkSwitch               *toggle)
{
  g_assert (EDITOR_IS_PREFERENCES_SWITCH (self));
  g_assert (GTK_IS_SWITCH (toggle));

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_ACTIVE]);
}

static void
editor_preferences_switch_constructed (GObject *object)
{
  EditorPreferencesSwitch *self = (EditorPreferencesSwitch *)object;
  g_autoptr(GSimpleActionGroup) group = NULL;
  g_autoptr(GAction) action = NULL;
  g_autofree gchar *name = NULL;

  G_OBJECT_CLASS (editor_preferences_switch_parent_class)->constructed (object);

  if (self->schema_id == NULL || self->schema_key == NULL)
    {
      g_warning ("Cannot setup preferences switch, missing schema properties");
      return;
    }

  self->settings = g_settings_new (self->schema_id);

  group = g_simple_action_group_new ();
  action = g_settings_create_action (self->settings, self->schema_key);
  g_action_map_add_action (G_ACTION_MAP (group), action);
  gtk_widget_insert_action_group (GTK_WIDGET (self), "settings", G_ACTION_GROUP (group));

  name = g_strdup_printf ("settings.%s", self->schema_key);
  gtk_actionable_set_action_name (GTK_ACTIONABLE (self->toggle), name);
}

static void
editor_preferences_switch_finalize (GObject *object)
{
  EditorPreferencesSwitch *self = (EditorPreferencesSwitch *)object;

  g_clear_object (&self->settings);
  g_clear_pointer (&self->schema_id, g_free);
  g_clear_pointer (&self->schema_key, g_free);

  G_OBJECT_CLASS (editor_preferences_switch_parent_class)->finalize (object);
}

static void
editor_preferences_switch_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  EditorPreferencesSwitch *self = EDITOR_PREFERENCES_SWITCH (object);

  switch (prop_id)
    {
    case PROP_ACTIVE:
      g_value_set_boolean (value, gtk_switch_get_active (self->toggle));
      break;

    case PROP_LABEL:
      g_value_set_string (value, gtk_label_get_label (self->label));
      break;

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
editor_preferences_switch_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  EditorPreferencesSwitch *self = EDITOR_PREFERENCES_SWITCH (object);

  switch (prop_id)
    {
    case PROP_ACTIVE:
      gtk_switch_set_active (self->toggle, g_value_get_boolean (value));
      break;

    case PROP_LABEL:
      gtk_label_set_label (self->label, g_value_get_string (value));
      break;

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
editor_preferences_switch_class_init (EditorPreferencesSwitchClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = editor_preferences_switch_constructed;
  object_class->finalize = editor_preferences_switch_finalize;
  object_class->get_property = editor_preferences_switch_get_property;
  object_class->set_property = editor_preferences_switch_set_property;

  properties [PROP_ACTIVE] =
    g_param_spec_boolean ("active",
                          "Active",
                          "If the switch is active",
                          FALSE,
                          (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties [PROP_LABEL] =
    g_param_spec_string ("label",
                         "Label",
                         "The label for the row",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

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
editor_preferences_switch_init (EditorPreferencesSwitch *self)
{
  self->toggle = g_object_new (GTK_TYPE_SWITCH,
                               "can-focus", FALSE,
                               "valign", GTK_ALIGN_CENTER,
                               NULL);
  g_signal_connect_object (self->toggle,
                           "notify::active",
                           G_CALLBACK (editor_preferences_switch_notify_active_cb),
                           self,
                           G_CONNECT_SWAPPED);

  adw_action_row_add_suffix (ADW_ACTION_ROW (self), GTK_WIDGET (self->toggle));
  adw_action_row_set_activatable_widget (ADW_ACTION_ROW (self), GTK_WIDGET (self->toggle));
}
