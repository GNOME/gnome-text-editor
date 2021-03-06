/* editor-page-settings-provider.c
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

#define G_LOG_DOMAIN "editor-page-settings-provider"

#include "config.h"

#include "editor-document.h"
#include "editor-page-settings-provider.h"

G_DEFINE_INTERFACE (EditorPageSettingsProvider, editor_page_settings_provider, G_TYPE_OBJECT)

enum {
  CHANGED,
  N_SIGNALS
};

static guint signals [N_SIGNALS];

static void
editor_page_settings_provider_default_init (EditorPageSettingsProviderInterface *iface)
{
  /**
   * EditorPageSettingsProvider::changed:
   * @self: an #EditorPageSettingsProvider
   *
   * The "changed" signal is emitted when any of the settings are changed
   * from the provider.
   *
   * #EditorPageSettings will ensure that changes are only emitted as
   * property notifications if they've changed from the previous value.
   */
  signals [CHANGED] =
    g_signal_new ("changed",
                  G_TYPE_FROM_INTERFACE (iface),
                  G_SIGNAL_RUN_LAST,
                  G_STRUCT_OFFSET (EditorPageSettingsProviderInterface, changed),
                  NULL, NULL, NULL, G_TYPE_NONE, 0);
}

void
editor_page_settings_provider_emit_changed (EditorPageSettingsProvider *self)
{
  g_return_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self));

  g_signal_emit (self, signals [CHANGED], 0);
}

void
editor_page_settings_provider_set_document (EditorPageSettingsProvider *self,
                                            EditorDocument             *document)
{
  g_return_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self));
  g_return_if_fail (!document || EDITOR_IS_DOCUMENT (document));

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->set_document)
    EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->set_document (self, document);
}

/**
 * editor_page_settings_provider_get_custom_font:
 * @self: an #EditorPageSettingsProvider
 * @custom_font: (out) (optional): a location for the custom font
 *
 * Gets the custom font if one is set by the provider.
 *
 * Returns: %TRUE if @custom_font was set by the provider.
 */
gboolean
editor_page_settings_provider_get_custom_font (EditorPageSettingsProvider  *self,
                                               gchar                      **custom_font)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (custom_font != NULL)
    *custom_font = FALSE;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_custom_font)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_custom_font (self, custom_font);

  return FALSE;
}

/**
 * editor_page_settings_provider_get_style_scheme:
 * @self: an #EditorPageSettingsProvider
 * @style_scheme: (out) (optional): a location for the style scheme
 *
 * Gets the style scheme if one is set by the provider.
 *
 * Returns: %TRUE if @style_scheme was set by the provider.
 */
gboolean
editor_page_settings_provider_get_style_scheme (EditorPageSettingsProvider  *self,
                                                gchar                      **style_scheme)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (style_scheme != NULL)
    *style_scheme = FALSE;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_style_scheme)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_style_scheme (self, style_scheme);

  return FALSE;
}

/**
 * editor_page_settings_provider_get_style_variant:
 * @self: an #EditorPageSettingsProvider
 * @style_variant: (out) (optional): a location for the style variant
 *
 * Gets the variant for the GTK and GtkSourceView themes.
 *
 * Returns: %TRUE if @style_variant was set by the provider.
 */
gboolean
editor_page_settings_provider_get_style_variant (EditorPageSettingsProvider  *self,
                                                 gchar                      **style_variant)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (style_variant != NULL)
    *style_variant = FALSE;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_style_variant)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_style_variant (self, style_variant);

  return FALSE;
}

/**
 * editor_page_settings_provider_get_insert_spaces_instead_of_tabs:
 * @self: a #EditorPageSettingsProvider
 * @insert_spaces_instead_of_tabs: (out) (optional): if spaces should be inserted
 *
 * Returns: %TRUE if @insert_spaces_instead_of_tabs was set by the provider.
 */
gboolean
editor_page_settings_provider_get_insert_spaces_instead_of_tabs (EditorPageSettingsProvider *self,
                                                                 gboolean                   *insert_spaces_instead_of_tabs)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (insert_spaces_instead_of_tabs != NULL)
    *insert_spaces_instead_of_tabs = FALSE;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_insert_spaces_instead_of_tabs)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_insert_spaces_instead_of_tabs (self, insert_spaces_instead_of_tabs);

  return FALSE;
}

/**
 * editor_page_settings_provider_get_show_line_numbers:
 * @self: a #EditorPageSettingsProvider
 * @show_line_numbers: (out) (optional): if line numbers should be shown
 *
 * Returns: %TRUE if @show_line_numbers was set by the provider.
 */
gboolean
editor_page_settings_provider_get_show_line_numbers (EditorPageSettingsProvider *self,
                                                     gboolean                   *show_line_numbers)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (show_line_numbers != NULL)
    *show_line_numbers = FALSE;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_show_line_numbers)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_show_line_numbers (self, show_line_numbers);

  return FALSE;
}

/**
 * editor_page_settings_provider_get_show_right_margin:
 * @self: a #EditorPageSettingsProvider
 * @show_right_margin: (out) (optional): if the right margin should be shown
 *
 * Returns: %TRUE if @show_right_margin was set by the provider.
 */
gboolean
editor_page_settings_provider_get_show_right_margin (EditorPageSettingsProvider *self,
                                                     gboolean                   *show_right_margin)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (show_right_margin != NULL)
    *show_right_margin = FALSE;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_show_right_margin)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_show_right_margin (self, show_right_margin);

  return FALSE;
}

/**
 * editor_page_settings_provider_get_use_system_font:
 * @self: a #EditorPageSettingsProvider
 * @wrap_text: (out) (optional): if the system font should be used
 *
 * Returns: %TRUE if @use_system_font was set by the provider.
 */
gboolean
editor_page_settings_provider_get_use_system_font (EditorPageSettingsProvider *self,
                                                   gboolean                   *use_system_font)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (use_system_font != NULL)
    *use_system_font = FALSE;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_use_system_font)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_use_system_font (self, use_system_font);

  return FALSE;
}

/**
 * editor_page_settings_provider_get_tab_width:
 * @self: a #EditorPageSettingsProvider
 * @wrap_text: (out) (optional): if the text should wrap
 *
 * Returns: %TRUE if @wrap_text was set by the provider.
 */
gboolean
editor_page_settings_provider_get_wrap_text (EditorPageSettingsProvider *self,
                                             gboolean                   *wrap_text)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (wrap_text != NULL)
    *wrap_text = FALSE;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_wrap_text)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_wrap_text (self, wrap_text);

  return FALSE;
}

/**
 * editor_page_settings_provider_get_tab_width:
 * @self: a #EditorPageSettingsProvider
 * @tab_width: (out) (optional): the width of tabs in spaces
 *
 * Returns: %TRUE if @tab_width was set by the provider.
 */
gboolean
editor_page_settings_provider_get_tab_width (EditorPageSettingsProvider *self,
                                             guint                      *tab_width)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (tab_width != NULL)
    *tab_width = FALSE;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_tab_width)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_tab_width (self, tab_width);

  return FALSE;
}

/**
 * editor_page_settings_provider_get_right_margin_position:
 * @self: a #EditorPageSettingsProvider
 * @right_margin_position: (out) (optional): the position for the right margin
 *
 * Returns: %TRUE if @right_margin_position was set by the provider.
 */
gboolean
editor_page_settings_provider_get_right_margin_position (EditorPageSettingsProvider *self,
                                                         guint                      *right_margin_position)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (right_margin_position != NULL)
    *right_margin_position = 80;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_right_margin_position)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_right_margin_position (self, right_margin_position);

  return FALSE;
}

/**
 * editor_page_settings_provider_get_show_map:
 * @self: a #EditorPageSettingsProvider
 * @show_map: (out) (optional): a location to store the setting
 *
 * Returns: %TRUE if @show_map was set by the provider.
 */
gboolean
editor_page_settings_provider_get_show_map (EditorPageSettingsProvider *self,
                                            gboolean                   *show_map)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (show_map != NULL)
    *show_map = FALSE;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_show_map)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_show_map (self, show_map);

  return FALSE;
}

/**
 * editor_page_settings_provider_get_show_grid:
 * @self: a #EditorPageSettingsProvider
 * @show_grid: (out) (optional): a location to store the setting
 *
 * Returns: %TRUE if @show_grid was set by the provider.
 */
gboolean
editor_page_settings_provider_get_show_grid (EditorPageSettingsProvider *self,
                                             gboolean                   *show_grid)
{
  g_return_val_if_fail (EDITOR_IS_PAGE_SETTINGS_PROVIDER (self), FALSE);

  if (show_grid != NULL)
    *show_grid = FALSE;

  if (EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_show_grid)
    return EDITOR_PAGE_SETTINGS_PROVIDER_GET_IFACE (self)->get_show_grid (self, show_grid);

  return FALSE;
}
