/* editor-page-settings.h
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

#define EDITOR_TYPE_PAGE_SETTINGS (editor_page_settings_get_type())

G_DECLARE_FINAL_TYPE (EditorPageSettings, editor_page_settings, EDITOR, PAGE_SETTINGS, GObject)

EditorPageSettings *editor_page_settings_new_for_document                  (EditorDocument     *document);
const gchar        *editor_page_settings_get_custom_font                   (EditorPageSettings *self);
const gchar        *editor_page_settings_get_style_scheme                  (EditorPageSettings *self);
const gchar        *editor_page_settings_get_style_variant                 (EditorPageSettings *self);
gboolean            editor_page_settings_get_insert_spaces_instead_of_tabs (EditorPageSettings *self);
guint               editor_page_settings_get_right_margin_position         (EditorPageSettings *self);
gboolean            editor_page_settings_get_auto_indent                   (EditorPageSettings *self);
gboolean            editor_page_settings_get_show_line_numbers             (EditorPageSettings *self);
gboolean            editor_page_settings_get_show_grid                     (EditorPageSettings *self);
gboolean            editor_page_settings_get_show_map                      (EditorPageSettings *self);
gboolean            editor_page_settings_get_show_right_margin             (EditorPageSettings *self);
gboolean            editor_page_settings_get_highlight_matching_brackets   (EditorPageSettings *self);
gboolean            editor_page_settings_get_use_system_font               (EditorPageSettings *self);
gboolean            editor_page_settings_get_wrap_text                     (EditorPageSettings *self);
guint               editor_page_settings_get_tab_width                     (EditorPageSettings *self);
int                 editor_page_settings_get_indent_width                  (EditorPageSettings *self);
gboolean            editor_page_settings_get_highlight_current_line        (EditorPageSettings *self);
gboolean            editor_page_settings_get_implicit_trailing_newline     (EditorPageSettings *self);
EditorIndentStyle   editor_page_settings_get_indent_style                  (EditorPageSettings *self);

G_END_DECLS
