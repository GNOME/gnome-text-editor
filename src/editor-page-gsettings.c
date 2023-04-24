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

#include "editor-application.h"
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

GSETTINGS_GETTER (int, int, indent_width, "indent-width")
GSETTINGS_GETTER (guint, uint, tab_width, "tab-width")
GSETTINGS_GETTER (gboolean, boolean, show_right_margin, "show-right-margin")
GSETTINGS_GETTER (gboolean, boolean, show_line_numbers, "show-line-numbers")
GSETTINGS_GETTER (gboolean, boolean, use_system_font, "use-system-font")
GSETTINGS_GETTER (gboolean, boolean, wrap_text, "wrap-text")
GSETTINGS_GETTER (gboolean, boolean, auto_indent, "auto-indent")
GSETTINGS_GETTER (gboolean, boolean, show_map, "show-map")
GSETTINGS_GETTER (gboolean, boolean, show_grid, "show-grid")
GSETTINGS_GETTER (gboolean, boolean, highlight_current_line, "highlight-current-line")
GSETTINGS_GETTER (gboolean, boolean, highlight_matching_brackets, "highlight-matching-brackets")
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
  *style_scheme = g_strdup (editor_application_get_style_scheme (EDITOR_APPLICATION_DEFAULT));
  return TRUE;
}

static gboolean
editor_page_gsettings_get_style_variant (EditorPageSettingsProvider  *provider,
                                         gchar                      **style_variant)
{
  *style_variant = g_settings_get_string (EDITOR_PAGE_GSETTINGS (provider)->settings, "style-variant");
  return TRUE;
}

static gboolean
editor_page_gsettings_get_custom_font (EditorPageSettingsProvider  *provider,
                                       gchar                      **custom_font)
{
  EditorPageGsettings *self = EDITOR_PAGE_GSETTINGS (provider);

  if (!g_settings_get_boolean (self->settings, "use-system-font"))
    *custom_font = g_settings_get_string (EDITOR_PAGE_GSETTINGS (provider)->settings, "custom-font");
  else
    *custom_font = NULL;

  return TRUE;
}

static void
editor_page_gsettings_change_event_cb (EditorPageGsettings *self,
                                       gpointer             keys,
                                       int                  n_keys,
                                       GSettings           *settings)
{
  g_assert (EDITOR_IS_PAGE_GSETTINGS (self));
  g_assert (G_IS_SETTINGS (settings));

  editor_page_settings_provider_emit_changed (EDITOR_PAGE_SETTINGS_PROVIDER (self));
}

static void
page_settings_provider_iface_init (EditorPageSettingsProviderInterface *iface)
{
  iface->get_custom_font = editor_page_gsettings_get_custom_font;
  iface->get_insert_spaces_instead_of_tabs = editor_page_gsettings_get_insert_spaces_instead_of_tabs;
  iface->get_right_margin_position = editor_page_gsettings_get_right_margin_position;
  iface->get_show_line_numbers = editor_page_gsettings_get_show_line_numbers;
  iface->get_show_map = editor_page_gsettings_get_show_map;
  iface->get_show_grid = editor_page_gsettings_get_show_grid;
  iface->get_show_right_margin = editor_page_gsettings_get_show_right_margin;
  iface->get_tab_width = editor_page_gsettings_get_tab_width;
  iface->get_indent_width = editor_page_gsettings_get_indent_width;
  iface->get_use_system_font = editor_page_gsettings_get_use_system_font;
  iface->get_wrap_text = editor_page_gsettings_get_wrap_text;
  iface->get_auto_indent = editor_page_gsettings_get_auto_indent;
  iface->get_style_scheme = editor_page_gsettings_get_style_scheme;
  iface->get_style_variant = editor_page_gsettings_get_style_variant;
  iface->get_highlight_current_line = editor_page_gsettings_get_highlight_current_line;
  iface->get_highlight_matching_brackets = editor_page_gsettings_get_highlight_matching_brackets;
}

G_DEFINE_TYPE_WITH_CODE (EditorPageGsettings, editor_page_gsettings, G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (EDITOR_TYPE_PAGE_SETTINGS_PROVIDER,
                                                page_settings_provider_iface_init))

static void
editor_page_gsettings_dispose (GObject *object)
{
  EditorPageGsettings *self = (EditorPageGsettings *)object;

  if (self->settings != NULL)
    {
      g_signal_handlers_disconnect_by_func (self->settings,
                                            G_CALLBACK (editor_page_gsettings_change_event_cb),
                                            self);
      g_clear_object (&self->settings);
    }

  G_OBJECT_CLASS (editor_page_gsettings_parent_class)->dispose (object);
}

static void
editor_page_gsettings_class_init (EditorPageGsettingsClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_page_gsettings_dispose;
}

static void
editor_page_gsettings_init (EditorPageGsettings *self)
{
}

static void
editor_page_gsettings_notify_style_scheme_cb (EditorPageGsettings *self,
                                              GParamSpec          *pspec,
                                              EditorApplication   *app)
{
  g_assert (EDITOR_IS_PAGE_GSETTINGS (self));
  g_assert (EDITOR_IS_APPLICATION (app));

  editor_page_settings_provider_emit_changed (EDITOR_PAGE_SETTINGS_PROVIDER (self));
}

EditorPageSettingsProvider *
_editor_page_gsettings_new (GSettings *settings)
{
  EditorPageGsettings *self;

  g_return_val_if_fail (G_IS_SETTINGS (settings), NULL);

  self = g_object_new (EDITOR_TYPE_PAGE_GSETTINGS, NULL);
  self->settings = g_object_ref (settings);

  g_signal_connect_object (self->settings,
                           "change-event",
                           G_CALLBACK (editor_page_gsettings_change_event_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (EDITOR_APPLICATION_DEFAULT,
                           "notify::style-scheme",
                           G_CALLBACK (editor_page_gsettings_notify_style_scheme_cb),
                           self,
                           G_CONNECT_SWAPPED);

  return EDITOR_PAGE_SETTINGS_PROVIDER (self);
}
