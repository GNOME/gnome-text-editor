/* editor-preferences-spin.c
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

#define G_LOG_DOMAIN "editor-preferences-spin"

#include "config.h"

#include "editor-preferences-spin.h"

struct _EditorPreferencesSpin
{
  AdwActionRow  row;

  GtkLabel             *label;
  GtkSpinButton        *spin;

  GSettings            *settings;
  gchar                *schema_id;
  gchar                *schema_key;
};

enum {
  PROP_0,
  PROP_LABEL,
  PROP_SCHEMA_ID,
  PROP_SCHEMA_KEY,
  N_PROPS
};

G_DEFINE_TYPE (EditorPreferencesSpin, editor_preferences_spin, ADW_TYPE_ACTION_ROW)

static GParamSpec *properties [N_PROPS];

static void
editor_preferences_spin_constructed (GObject *object)
{
  EditorPreferencesSpin *self = (EditorPreferencesSpin *)object;
  GSettingsSchemaSource *source = NULL;
  g_autoptr(GSettingsSchemaKey) key = NULL;
  g_autoptr(GSettingsSchema) schema = NULL;
  g_autoptr(GVariant) range = NULL;
  g_autofree gchar *kind = NULL;
  g_autoptr(GVariant) values = NULL;
  GtkAdjustment *adj;

  G_OBJECT_CLASS (editor_preferences_spin_parent_class)->constructed (object);

  if (self->schema_id == NULL || self->schema_key == NULL)
    {
      g_warning ("Cannot setup preferences spin, missing schema properties");
      return;
    }

  source = g_settings_schema_source_get_default ();
  schema = g_settings_schema_source_lookup (source, self->schema_id, TRUE);

  if (schema == NULL || !g_settings_schema_has_key (schema, self->schema_key))
    {
      g_warning ("Failed to locate schema %s", self->schema_id);
      return;
    }

  adj = gtk_spin_button_get_adjustment (self->spin);
  gtk_adjustment_set_step_increment (adj, 1);
  gtk_adjustment_set_page_increment (adj, 10);

  key = g_settings_schema_get_key (schema, self->schema_key);
  range = g_settings_schema_key_get_range (key);

  g_variant_get (range, "(sv)", &kind, &values);

  if (g_strcmp0 (kind, "range") == 0)
    {
      const GVariantType *type = g_settings_schema_key_get_value_type (key);

      if (g_variant_type_is_subtype_of (type, G_VARIANT_TYPE_UINT32))
        {
          guint min, max;
          g_variant_get (values, "(uu)", &min, &max);
          gtk_adjustment_set_lower (adj, min);
          gtk_adjustment_set_upper (adj, max);
        }
      else if (g_variant_type_is_subtype_of (type, G_VARIANT_TYPE_INT32))
        {
          gint min, max;
          g_variant_get (values, "(ii)", &min, &max);
          gtk_adjustment_set_lower (adj, min);
          gtk_adjustment_set_upper (adj, max);
        }
      else if (g_variant_type_is_subtype_of (type, G_VARIANT_TYPE_DOUBLE))
        {
          gdouble min, max;
          g_variant_get (values, "(dd)", &min, &max);
          gtk_adjustment_set_lower (adj, min);
          gtk_adjustment_set_upper (adj, max);
        }
    }

  self->settings = g_settings_new (self->schema_id);

  g_settings_bind (self->settings, self->schema_key,
                   adj, "value",
                   G_SETTINGS_BIND_DEFAULT);

  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (object), TRUE);

  gtk_widget_add_css_class (GTK_WIDGET (self), "spin");
}

static void
editor_preferences_spin_activated (AdwActionRow *row)
{
  EditorPreferencesSpin *self = (EditorPreferencesSpin *)row;

  g_assert (ADW_IS_ACTION_ROW (self));

  gtk_widget_grab_focus (GTK_WIDGET (self->spin));
}

static void
editor_preferences_spin_finalize (GObject *object)
{
  EditorPreferencesSpin *self = (EditorPreferencesSpin *)object;

  g_clear_object (&self->settings);
  g_clear_pointer (&self->schema_id, g_free);
  g_clear_pointer (&self->schema_key, g_free);

  G_OBJECT_CLASS (editor_preferences_spin_parent_class)->finalize (object);
}

static void
editor_preferences_spin_get_property (GObject    *object,
                                        guint       prop_id,
                                        GValue     *value,
                                        GParamSpec *pspec)
{
  EditorPreferencesSpin *self = EDITOR_PREFERENCES_SPIN (object);

  switch (prop_id)
    {
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
editor_preferences_spin_set_property (GObject      *object,
                                        guint         prop_id,
                                        const GValue *value,
                                        GParamSpec   *pspec)
{
  EditorPreferencesSpin *self = EDITOR_PREFERENCES_SPIN (object);

  switch (prop_id)
    {
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
editor_preferences_spin_class_init (EditorPreferencesSpinClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  AdwActionRowClass *row_class = ADW_ACTION_ROW_CLASS (klass);

  object_class->constructed = editor_preferences_spin_constructed;
  object_class->finalize = editor_preferences_spin_finalize;
  object_class->get_property = editor_preferences_spin_get_property;
  object_class->set_property = editor_preferences_spin_set_property;

  row_class->activate = editor_preferences_spin_activated;

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
editor_preferences_spin_init (EditorPreferencesSpin *self)
{
  self->spin = g_object_new (GTK_TYPE_SPIN_BUTTON,
                             "can-focus", TRUE,
                             "valign", GTK_ALIGN_CENTER,
                             NULL);
  adw_action_row_add_suffix (ADW_ACTION_ROW (self), GTK_WIDGET (self->spin));
}
