/* editor-page-gsettings.c
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

#define G_LOG_DOMAIN "editor-page-gsettings"

#include "config.h"

#include "editor-page-gsettings-private.h"

struct _EditorPageGsettings
{
  GObject    parent_instance;
  GSettings *settings;
};

#define GSETTINGS_GETTER(type, getfunc, name, key)                      \
static gboolean                                                         \
editor_page_gsettings_get_##name (EditorPageSettingsProvider *provider, \
                                  type                       *outval)   \
{                                                                       \
  EditorPageGsettings *self = EDITOR_PAGE_GSETTINGS (provider);         \
  if (outval)                                                           \
    *outval = g_settings_get_##getfunc (self->settings, key);           \
  return TRUE;                                                          \
}

GSETTINGS_GETTER (guint, uint, tab_width, "tab-width")
GSETTINGS_GETTER (gboolean, boolean, show_right_margin, "show-right-margin")
GSETTINGS_GETTER (gboolean, boolean, show_line_numbers, "show-line-numbers")
GSETTINGS_GETTER (gboolean, boolean, use_system_font, "use-system-font")
GSETTINGS_GETTER (gboolean, boolean, dark_mode, "dark-mode")
GSETTINGS_GETTER (gboolean, boolean, wrap_text, "wrap-text")
GSETTINGS_GETTER (guint, uint, right_margin_position, "right-margin-position")

static gboolean
editor_page_gsettings_get_insert_spaces_instead_of_tabs (EditorPageSettingsProvider *provider,
                                                         gboolean                   *insert_spaces_instead_of_tabs)
{
  EditorPageGsettings *self = EDITOR_PAGE_GSETTINGS (provider);

  if (insert_spaces_instead_of_tabs)
    {
      g_autofree gchar *val = g_settings_get_string (self->settings, "indent-style");
      *insert_spaces_instead_of_tabs = g_strcmp0 (val, "space") == 0;
    }

  return TRUE;
}

static gboolean
editor_page_gsettings_get_style_scheme (EditorPageSettingsProvider  *provider,
                                        gchar                      **style_scheme)
{
  EditorPageGsettings *self = EDITOR_PAGE_GSETTINGS (provider);
  GtkSourceStyleSchemeManager *sm;
  GtkSourceStyleScheme *scheme;
  g_autofree gchar *light = NULL;
  g_autofree gchar *dark = NULL;
  g_autofree gchar *regular = NULL;

  sm = gtk_source_style_scheme_manager_get_default ();
  regular = g_settings_get_string (self->settings, "style-scheme");

  if (g_str_has_suffix (regular, "-dark"))
    {
      regular[strlen(regular)-5] = 0;
      dark = g_strdup_printf ("%s-dark", regular);
      light = g_strdup_printf ("%s-light", regular);
    }
  else if (g_str_has_suffix (regular, "-light"))
    {
      regular[strlen(regular)-6] = 0;
      dark = g_strdup_printf ("%s-dark", regular);
      light = g_strdup_printf ("%s-light", regular);
    }
  else
    {
      dark = g_strdup_printf ("%s-dark", regular);
      light = g_strdup_printf ("%s-light", regular);
    }

  if (g_settings_get_boolean (self->settings, "dark-mode"))
    {
      if (!(scheme = gtk_source_style_scheme_manager_get_scheme (sm, dark)) &&
          !(scheme = gtk_source_style_scheme_manager_get_scheme (sm, regular)) &&
          !(scheme = gtk_source_style_scheme_manager_get_scheme (sm, light)))
        scheme = gtk_source_style_scheme_manager_get_scheme (sm, "Adwaita-dark");
    }
  else
    {
      if (!(scheme = gtk_source_style_scheme_manager_get_scheme (sm, light)) &&
          !(scheme = gtk_source_style_scheme_manager_get_scheme (sm, regular)) &&
          !(scheme = gtk_source_style_scheme_manager_get_scheme (sm, dark)))
        scheme = gtk_source_style_scheme_manager_get_scheme (sm, "Adwaita");
    }

  if (style_scheme)
    {
      if (scheme)
        *style_scheme = g_strdup (gtk_source_style_scheme_get_id (scheme));
      else
        *style_scheme = NULL;
    }

  return TRUE;
}

static void
editor_page_gsettings_changed_cb (EditorPageGsettings *self,
                                  const gchar         *key,
                                  GSettings           *settings)
{
  g_assert (EDITOR_IS_PAGE_GSETTINGS (self));
  g_assert (G_IS_SETTINGS (settings));

  editor_page_settings_provider_emit_changed (EDITOR_PAGE_SETTINGS_PROVIDER (self));
}

static void
page_settings_provider_iface_init (EditorPageSettingsProviderInterface *iface)
{
  iface->get_dark_mode = editor_page_gsettings_get_dark_mode;
  iface->get_insert_spaces_instead_of_tabs = editor_page_gsettings_get_insert_spaces_instead_of_tabs;
  iface->get_right_margin_position = editor_page_gsettings_get_right_margin_position;
  iface->get_show_line_numbers = editor_page_gsettings_get_show_line_numbers;
  iface->get_show_right_margin = editor_page_gsettings_get_show_right_margin;
  iface->get_tab_width = editor_page_gsettings_get_tab_width;
  iface->get_use_system_font = editor_page_gsettings_get_use_system_font;
  iface->get_wrap_text = editor_page_gsettings_get_wrap_text;
  iface->get_style_scheme = editor_page_gsettings_get_style_scheme;
}

G_DEFINE_TYPE_WITH_CODE (EditorPageGsettings, editor_page_gsettings, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (EDITOR_TYPE_PAGE_SETTINGS_PROVIDER,
                                                page_settings_provider_iface_init))

static void
editor_page_gsettings_finalize (GObject *object)
{
  EditorPageGsettings *self = (EditorPageGsettings *)object;

  g_clear_object (&self->settings);

  G_OBJECT_CLASS (editor_page_gsettings_parent_class)->finalize (object);
}

static void
editor_page_gsettings_class_init (EditorPageGsettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = editor_page_gsettings_finalize;
}

static void
editor_page_gsettings_init (EditorPageGsettings *self)
{
  self->settings = g_settings_new ("org.gnome.TextEditor");

  g_signal_connect_object (self->settings,
                           "changed",
                           G_CALLBACK (editor_page_gsettings_changed_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

EditorPageSettingsProvider *
_editor_page_gsettings_new (void)
{
  return g_object_new (EDITOR_TYPE_PAGE_GSETTINGS, NULL);
}
