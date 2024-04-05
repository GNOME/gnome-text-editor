/* editor-page-settings-provider.h
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

#pragma once

#include "editor-types.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_PAGE_SETTINGS_PROVIDER (editor_page_settings_provider_get_type())

G_DECLARE_INTERFACE (EditorPageSettingsProvider, editor_page_settings_provider, EDITOR, PAGE_SETTINGS_PROVIDER, GObject)

struct _EditorPageSettingsProviderInterface
{
  GTypeInterface parent_iface;

  void      (*set_document)                      (EditorPageSettingsProvider *self,
                                                  EditorDocument             *document);
  void      (*changed)                           (EditorPageSettingsProvider  *self);
  gboolean  (*get_custom_font)                   (EditorPageSettingsProvider  *self,
                                                  gchar                      **custom_font);
  gboolean  (*get_style_scheme)                  (EditorPageSettingsProvider  *self,
                                                  gchar                      **style_scheme);
  gboolean  (*get_style_variant)                 (EditorPageSettingsProvider  *self,
                                                  gchar                      **style_variant);
  gboolean  (*get_insert_spaces_instead_of_tabs) (EditorPageSettingsProvider *self,
                                                  gboolean                   *insert_spaces_instead_of_tabs);
  gboolean  (*get_right_margin_position)         (EditorPageSettingsProvider *self,
                                                  guint                      *right_margin_position);
  gboolean  (*get_show_line_numbers)             (EditorPageSettingsProvider *self,
                                                  gboolean                   *show_line_numbers);
  gboolean  (*get_show_right_margin)             (EditorPageSettingsProvider *self,
                                                  gboolean                   *show_right_margin);
  gboolean  (*get_use_system_font)               (EditorPageSettingsProvider *self,
                                                  gboolean                   *use_system_font);
  gboolean  (*get_wrap_text)                     (EditorPageSettingsProvider *self,
                                                  gboolean                   *wrap_text);
  gboolean  (*get_tab_width)                     (EditorPageSettingsProvider *self,
                                                  guint                      *tab_width);
  gboolean  (*get_show_map)                      (EditorPageSettingsProvider *self,
                                                  gboolean                   *show_map);
  gboolean  (*get_show_grid)                     (EditorPageSettingsProvider *self,
                                                  gboolean                   *show_grid);
  gboolean  (*get_highlight_current_line)        (EditorPageSettingsProvider *self,
                                                  gboolean                   *highlight_current_line);
  gboolean  (*get_auto_indent)                   (EditorPageSettingsProvider *self,
                                                  gboolean                   *auto_indent);
  gboolean  (*get_indent_width)                  (EditorPageSettingsProvider *self,
                                                  int                        *indent_width);
  gboolean  (*get_highlight_matching_brackets)   (EditorPageSettingsProvider *self,
                                                  gboolean                   *highlight_matching_brackets);
  gboolean  (*get_implicit_trailing_newline)     (EditorPageSettingsProvider *self,
                                                  gboolean                   *implicit_trailing_newline);
};

void     editor_page_settings_provider_emit_changed                      (EditorPageSettingsProvider  *self);
void     editor_page_settings_provider_set_document                      (EditorPageSettingsProvider  *self,
                                                                          EditorDocument              *document);
gboolean editor_page_settings_provider_get_custom_font                   (EditorPageSettingsProvider  *self,
                                                                          gchar                      **custom_font);
gboolean editor_page_settings_provider_get_style_scheme                  (EditorPageSettingsProvider  *self,
                                                                          gchar                      **style_scheme);
gboolean editor_page_settings_provider_get_style_variant                 (EditorPageSettingsProvider  *self,
                                                                          gchar                      **style_variant);
gboolean editor_page_settings_provider_get_dark_mode                     (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *dark_mode);
gboolean editor_page_settings_provider_get_insert_spaces_instead_of_tabs (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *insert_spaces_instead_of_tabs);
gboolean editor_page_settings_provider_get_right_margin_position         (EditorPageSettingsProvider  *self,
                                                                          guint                       *right_margin_position);
gboolean editor_page_settings_provider_get_show_line_numbers             (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *show_line_numbers);
gboolean editor_page_settings_provider_get_show_right_margin             (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *show_right_margin);
gboolean editor_page_settings_provider_get_use_system_font               (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *use_system_font);
gboolean editor_page_settings_provider_get_auto_indent                   (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *auto_indent);
gboolean editor_page_settings_provider_get_wrap_text                     (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *wrap_text);
gboolean editor_page_settings_provider_get_tab_width                     (EditorPageSettingsProvider  *self,
                                                                          guint                       *tab_width);
gboolean editor_page_settings_provider_get_implicit_trailing_newline     (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *implicit_trailing_newline);
gboolean editor_page_settings_provider_get_indent_width                  (EditorPageSettingsProvider  *self,
                                                                          int                         *indent_width);
gboolean editor_page_settings_provider_get_show_map                      (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *show_map);
gboolean editor_page_settings_provider_get_show_grid                     (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *show_grid);
gboolean editor_page_settings_provider_get_highlight_current_line        (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *highlight_current_line);
gboolean editor_page_settings_provider_get_highlight_matching_brackets   (EditorPageSettingsProvider  *self,
                                                                          gboolean                    *highlight_current_line);

G_END_DECLS
