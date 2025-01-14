/* editor-gsettings-addin.c
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "config.h"

#include <gtksourceview/gtksource.h>

#include "editor-document.h"
#include "editor-gsettings-addin.h"

struct _EditorGsettingsAddin
{
  EditorSettingsAddin  parent_instance;
  GSettings           *settings;
};

G_DEFINE_FINAL_TYPE (EditorGsettingsAddin, editor_gsettings_addin, EDITOR_TYPE_SETTINGS_ADDIN)

static DexFuture *
editor_gsettings_addin_load (EditorSettingsAddin *settings_addin,
                             EditorDocument      *document,
                             GFile               *file)
{
  EditorGsettingsAddin *self = (EditorGsettingsAddin *)settings_addin;

  g_assert (EDITOR_IS_GSETTINGS_ADDIN (self));
  g_assert (EDITOR_IS_DOCUMENT (document));
  g_assert (!file || G_IS_FILE (file));

  return dex_future_new_true ();
}

static gboolean
apply_boolean (GSettings  *settings,
               GValue     *value,
               const char *key)
{
  g_autoptr(GVariant) variant = g_settings_get_user_value (settings, key);

  if (variant != NULL)
    {
      g_value_set_boolean (value, g_variant_get_boolean (variant));
      return TRUE;
    }

  return FALSE;
}

static gboolean
apply_uint (GSettings  *settings,
            GValue     *value,
            const char *key)
{
  g_autoptr(GVariant) variant = g_settings_get_user_value (settings, key);

  if (variant != NULL)
    {
      g_value_set_uint (value, g_variant_get_uint32 (variant));
      return TRUE;
    }

  return FALSE;
}

static gboolean
apply_int (GSettings  *settings,
           GValue     *value,
           const char *key)
{
  g_autoptr(GVariant) variant = g_settings_get_user_value (settings, key);

  if (variant != NULL)
    {
      g_value_set_int (value, g_variant_get_int32 (variant));
      return TRUE;
    }

  return FALSE;
}

static gboolean
apply_double (GSettings  *settings,
              GValue     *value,
              const char *key)
{
  g_autoptr(GVariant) variant = g_settings_get_user_value (settings, key);

  if (variant != NULL)
    {
      g_value_set_double (value, g_variant_get_double (variant));
      return TRUE;
    }

  return FALSE;
}

static gboolean
apply_string (GSettings  *settings,
              GValue     *value,
              const char *key)
{
  g_autoptr(GVariant) variant = g_settings_get_user_value (settings, key);

  if (variant != NULL)
    {
      g_value_set_string (value, g_variant_get_string (variant, NULL));
      return TRUE;
    }

  return FALSE;
}

static gboolean
editor_gsettings_addin_get_value (EditorSettingsAddin  *settings_addin,
                                  EditorSettingsOption  option,
                                  GValue               *value)
{
  EditorGsettingsAddin *self = EDITOR_GSETTINGS_ADDIN (settings_addin);

  switch (option)
    {
    case EDITOR_SETTINGS_OPTION_AUTO_INDENT:
      return apply_boolean (self->settings, value, "auto-indent");

    case EDITOR_SETTINGS_OPTION_DISCOVER_SETTINGS:
      return apply_boolean (self->settings, value, "discover-settings");

    case EDITOR_SETTINGS_OPTION_ENABLE_SPELLCHECK:
      return apply_boolean (self->settings, value, "spellcheck");

    case EDITOR_SETTINGS_OPTION_INSERT_SPACES_INSTEAD_OF_TABS:
      {
        g_autoptr(GVariant) variant = g_settings_get_user_value (self->settings, "indent-style");

        if (variant != NULL)
          {
            g_value_set_boolean (value, g_strcmp0 (g_variant_get_string (variant, NULL), "space") == 0);
            return TRUE;
          }

        return FALSE;
      }

    case EDITOR_SETTINGS_OPTION_SHOW_LINE_NUMBERS:
      return apply_boolean (self->settings, value, "show-line-numbers");

    case EDITOR_SETTINGS_OPTION_SHOW_RIGHT_MARGIN:
      return apply_boolean (self->settings, value, "show-right-margin");

    case EDITOR_SETTINGS_OPTION_WRAP_TEXT:
      return apply_boolean (self->settings, value, "wrap-text");

    case EDITOR_SETTINGS_OPTION_RIGHT_MARGIN_POSITION:
      return apply_uint (self->settings, value, "right-margin-position");

    case EDITOR_SETTINGS_OPTION_TAB_WIDTH:
      return apply_uint (self->settings, value, "tab-width");

    case EDITOR_SETTINGS_OPTION_INDENT_WIDTH:
      return apply_int (self->settings, value, "indent-width");

    case EDITOR_SETTINGS_OPTION_HIGHLIGHT_CURRENT_LINE:
      return apply_boolean (self->settings, value, "highlight-current-line");

    case EDITOR_SETTINGS_OPTION_HIGHLIGHT_MATCHING_BRACKETS:
      return apply_boolean (self->settings, value, "highlight-matching-brackets");

    case EDITOR_SETTINGS_OPTION_ENABLE_SNIPPETS:
      return apply_boolean (self->settings, value, "enable-snippets");

    case EDITOR_SETTINGS_OPTION_KEYBINDINGS:
      return apply_string (self->settings, value, "keybindings");

    case EDITOR_SETTINGS_OPTION_LINE_HEIGHT:
      return apply_double (self->settings, value, "line-height");

    case EDITOR_SETTINGS_OPTION_DRAW_SPACES:
      {
        GtkSourceSpaceLocationFlags location_flags = GTK_SOURCE_SPACE_LOCATION_NONE;
        GtkSourceSpaceTypeFlags type_flags = GTK_SOURCE_SPACE_TYPE_NONE;
        g_autoptr(GtkSourceSpaceDrawer) drawer = gtk_source_space_drawer_new ();
        guint flags = g_settings_get_flags (self->settings, "draw-spaces");

        if (flags & 1)
          type_flags |= GTK_SOURCE_SPACE_TYPE_SPACE;

        if (flags & 2)
          type_flags |= GTK_SOURCE_SPACE_TYPE_TAB;

        if (flags & 4)
          {
            gtk_source_space_drawer_set_types_for_locations (drawer, GTK_SOURCE_SPACE_LOCATION_ALL, GTK_SOURCE_SPACE_TYPE_NEWLINE);
            type_flags |= GTK_SOURCE_SPACE_TYPE_NEWLINE;
          }

        if (flags & 8)
          type_flags |= GTK_SOURCE_SPACE_TYPE_NBSP;

        if (flags & 16)
          location_flags |= GTK_SOURCE_SPACE_LOCATION_LEADING;

        if (flags & 32)
          location_flags |= GTK_SOURCE_SPACE_LOCATION_INSIDE_TEXT;

        if (flags & 64)
          location_flags |= GTK_SOURCE_SPACE_LOCATION_TRAILING;

        if (type_flags > 0 && location_flags == 0)
          location_flags |= GTK_SOURCE_SPACE_LOCATION_ALL;

        gtk_source_space_drawer_set_types_for_locations (drawer, location_flags, type_flags);
        gtk_source_space_drawer_set_enable_matrix (drawer, flags != 0);

        g_value_take_object (value, g_steal_pointer (&drawer));

        return TRUE;
      }

    case EDITOR_SETTINGS_OPTION_IMPLICIT_TRAILING_NEWLINE:
    case EDITOR_SETTINGS_OPTION_ENCODING:
    case EDITOR_SETTINGS_OPTION_LANGUAGE:
    case EDITOR_SETTINGS_OPTION_SYNTAX:
    case EDITOR_SETTINGS_OPTION_NEWLINE_TYPE:
    case EDITOR_SETTINGS_OPTION_ZOOM:
      return FALSE;

    default:
      g_return_val_if_reached (FALSE);
    }

  return FALSE;
}

static void
editor_gsettings_addin_changed_cb (EditorSettingsAddin *self,
                                   const char          *key,
                                   GSettings           *settings)
{
  g_assert (EDITOR_IS_GSETTINGS_ADDIN (self));
  g_assert (key != NULL);
  g_assert (G_IS_SETTINGS (settings));

  if (FALSE) {}
  else if (g_strcmp0 (key, "indent-width") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_INDENT_WIDTH);
  else if (g_strcmp0 (key, "tab-width") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_TAB_WIDTH);
  else if (g_strcmp0 (key, "right-margin-position") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_RIGHT_MARGIN_POSITION);
  else if (g_strcmp0 (key, "wrap-text") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_WRAP_TEXT);
  else if (g_strcmp0 (key, "show-right-margin") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_SHOW_RIGHT_MARGIN);
  else if (g_strcmp0 (key, "show-line-numbers") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_SHOW_LINE_NUMBERS);
  else if (g_strcmp0 (key, "indent-style") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_INSERT_SPACES_INSTEAD_OF_TABS);
  else if (g_strcmp0 (key, "spellcheck") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_ENABLE_SPELLCHECK);
  else if (g_strcmp0 (key, "discover-settings") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_DISCOVER_SETTINGS);
  else if (g_strcmp0 (key, "auto-indent") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_AUTO_INDENT);
  else if (g_strcmp0 (key, "highlight-matching-brackets") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_HIGHLIGHT_MATCHING_BRACKETS);
  else if (g_strcmp0 (key, "highlight-current-line") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_HIGHLIGHT_CURRENT_LINE);
  else if (g_strcmp0 (key, "draw-spaces") == 0)
    editor_settings_addin_changed (self, EDITOR_SETTINGS_OPTION_DRAW_SPACES);
}

static void
editor_gsettings_addin_finalize (GObject *object)
{
  EditorGsettingsAddin *self = (EditorGsettingsAddin *)object;

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (editor_gsettings_addin_parent_class)->finalize (object);
}

static void
editor_gsettings_addin_class_init (EditorGsettingsAddinClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  EditorSettingsAddinClass *settings_addin_class = EDITOR_SETTINGS_ADDIN_CLASS (klass);

  object_class->finalize = editor_gsettings_addin_finalize;

  settings_addin_class->load = editor_gsettings_addin_load;
  settings_addin_class->get_value = editor_gsettings_addin_get_value;
}

static void
editor_gsettings_addin_init (EditorGsettingsAddin *self)
{
  self->settings = g_settings_new ("org.gnome.TextEditor");

  g_signal_connect_object (self->settings,
                           "changed",
                           G_CALLBACK (editor_gsettings_addin_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
}
