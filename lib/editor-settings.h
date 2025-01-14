/*
 * editor-settings.h
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include <libdex.h>
#include <gtksourceview/gtksource.h>

#include "editor-types.h"
#include "editor-zoom.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_SETTINGS (editor_settings_get_type())

G_DECLARE_FINAL_TYPE (EditorSettings, editor_settings, EDITOR, SETTINGS, GObject)

EditorSettings       *editor_settings_new                               (EditorDocument       *document) G_GNUC_WARN_UNUSED_RESULT;
DexFuture            *editor_settings_save                              (EditorSettings       *self,
                                                                         GFile                *file) G_GNUC_WARN_UNUSED_RESULT;
DexFuture            *editor_settings_load                              (EditorSettings       *self,
                                                                         GFile                *file) G_GNUC_WARN_UNUSED_RESULT;
EditorDocument       *editor_settings_dup_document                      (EditorSettings       *self) G_GNUC_WARN_UNUSED_RESULT;
gboolean              editor_settings_get_auto_indent                   (EditorSettings       *self);
void                  editor_settings_set_auto_indent                   (EditorSettings       *self,
                                                                         gboolean              auto_indent);
gboolean              editor_settings_get_discover_settings             (EditorSettings       *self);
void                  editor_settings_set_discover_settings             (EditorSettings       *self,
                                                                         gboolean              discover_settings);
GtkSourceSpaceDrawer *editor_settings_dup_draw_spaces                   (EditorSettings       *self) G_GNUC_WARN_UNUSED_RESULT;
void                  editor_settings_set_draw_spaces                   (EditorSettings       *self,
                                                                         GtkSourceSpaceDrawer *draw_spaces);
gboolean              editor_settings_get_enable_snippets               (EditorSettings       *self);
void                  editor_settings_set_enable_snippets               (EditorSettings       *self,
                                                                         gboolean              enable_snippets);
gboolean              editor_settings_get_enable_spellcheck             (EditorSettings       *self);
void                  editor_settings_set_enable_spellcheck             (EditorSettings       *self,
                                                                         gboolean              enable_spellcheck);
char                 *editor_settings_dup_encoding                      (EditorSettings       *self) G_GNUC_WARN_UNUSED_RESULT;
void                  editor_settings_set_encoding                      (EditorSettings       *self,
                                                                         const char           *encoding);
gboolean              editor_settings_get_highlight_current_line        (EditorSettings       *self);
void                  editor_settings_set_highlight_current_line        (EditorSettings       *self,
                                                                         gboolean              highlight_current_line);
gboolean              editor_settings_get_highlight_matching_brackets   (EditorSettings       *self);
void                  editor_settings_set_highlight_matching_brackets   (EditorSettings       *self,
                                                                         gboolean              highlight_matching_brackets);
gboolean              editor_settings_get_implicit_trailing_newline     (EditorSettings       *self);
void                  editor_settings_set_implicit_trailing_newline     (EditorSettings       *self,
                                                                         gboolean              implicit_trailing_newline);
int                   editor_settings_get_indent_width                  (EditorSettings       *self);
void                  editor_settings_set_indent_width                  (EditorSettings       *self,
                                                                         int                   indent_width);
gboolean              editor_settings_get_insert_spaces_instead_of_tabs (EditorSettings       *self);
void                  editor_settings_set_insert_spaces_instead_of_tabs (EditorSettings       *self,
                                                                         gboolean              show_right_margin);
char                 *editor_settings_dup_keybindings                   (EditorSettings       *self) G_GNUC_WARN_UNUSED_RESULT;
void                  editor_settings_set_keybindings                   (EditorSettings       *self,
                                                                         const char           *keybindings);
char                 *editor_settings_dup_language                      (EditorSettings       *self) G_GNUC_WARN_UNUSED_RESULT;
void                  editor_settings_set_language                      (EditorSettings       *self,
                                                                         const char           *language);
double                editor_settings_get_line_height                   (EditorSettings       *self);
void                  editor_settings_set_line_height                   (EditorSettings       *self,
                                                                         double                line_height);
GtkSourceNewlineType  editor_settings_get_newline_type                  (EditorSettings       *self);
void                  editor_settings_set_newline_type                  (EditorSettings       *self,
                                                                         GtkSourceNewlineType  newline_type);
guint                 editor_settings_get_right_margin_position         (EditorSettings       *self);
void                  editor_settings_set_right_margin_position         (EditorSettings       *self,
                                                                         guint                 right_margin_position);
gboolean              editor_settings_get_show_line_numbers             (EditorSettings       *self);
void                  editor_settings_set_show_line_numbers             (EditorSettings       *self,
                                                                         gboolean              show_line_numbers);
gboolean              editor_settings_get_show_right_margin             (EditorSettings       *self);
void                  editor_settings_set_show_right_margin             (EditorSettings       *self,
                                                                         gboolean              show_right_margin);
char                 *editor_settings_dup_syntax                        (EditorSettings       *self) G_GNUC_WARN_UNUSED_RESULT;
void                  editor_settings_set_syntax                        (EditorSettings       *self,
                                                                         const char           *syntax);
guint                 editor_settings_get_tab_width                     (EditorSettings       *self);
void                  editor_settings_set_tab_width                     (EditorSettings       *self,
                                                                         guint                 indent_width);
gboolean              editor_settings_get_wrap_text                     (EditorSettings       *self);
void                  editor_settings_set_wrap_text                     (EditorSettings       *self,
                                                                         gboolean              wrap_text);
EditorZoom            editor_settings_get_zoom                          (EditorSettings       *self);
void                  editor_settings_set_zoom                          (EditorSettings       *self,
                                                                         EditorZoom            zoom);

G_END_DECLS
