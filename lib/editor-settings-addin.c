/*
 * editor-settings-addin.c
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

#include "config.h"

#include "editor-document.h"
#include "editor-settings-addin-private.h"

G_DEFINE_ABSTRACT_TYPE (EditorSettingsAddin, editor_settings_addin, G_TYPE_OBJECT)

enum {
  CHANGED,
  N_SIGNALS
};

static guint signals[N_SIGNALS];

static void
editor_settings_addin_class_init (EditorSettingsAddinClass *klass)
{
  signals[CHANGED] = g_signal_new ("changed",
                                   G_TYPE_FROM_CLASS (klass),
                                   G_SIGNAL_RUN_LAST,
                                   G_STRUCT_OFFSET (EditorSettingsAddinClass, changed),
                                   NULL, NULL,
                                   NULL,
                                   G_TYPE_NONE, 1, EDITOR_TYPE_SETTINGS_OPTION);
}

static void
editor_settings_addin_init (EditorSettingsAddin *self)
{
}

DexFuture *
editor_settings_addin_load (EditorSettingsAddin *self,
                            EditorDocument      *document,
                            GFile               *file)
{
  dex_return_error_if_fail (EDITOR_IS_SETTINGS_ADDIN (self));
  dex_return_error_if_fail (EDITOR_IS_DOCUMENT (document));
  dex_return_error_if_fail (!file || G_IS_FILE (file));

  return EDITOR_SETTINGS_ADDIN_GET_CLASS (self)->load (self, document, file);
}

gboolean
editor_settings_addin_get_value (EditorSettingsAddin  *self,
                                 EditorSettingsOption  option,
                                 GValue               *value)
{
  g_return_val_if_fail (EDITOR_IS_SETTINGS_ADDIN (self), FALSE);
  g_return_val_if_fail (G_IS_VALUE (value), FALSE);

  return EDITOR_SETTINGS_ADDIN_GET_CLASS (self)->get_value (self, option, value);
}

void
editor_settings_addin_changed (EditorSettingsAddin  *self,
                               EditorSettingsOption  option)
{
  g_return_if_fail (EDITOR_IS_SETTINGS_ADDIN (self));

  g_signal_emit (self, signals[CHANGED], 0, option);
}

G_DEFINE_ENUM_TYPE (EditorSettingsOption, editor_settings_option,
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_AUTO_INDENT, "auto-indent"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_DISCOVER_SETTINGS, "discover-settings"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_DRAW_SPACES, "draw-spaces"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_ENABLE_SNIPPETS, "enable-snippets"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_ENABLE_SPELLCHECK, "enable-spellcheck"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_ENCODING, "encoding"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_HIGHLIGHT_CURRENT_LINE, "highlight-current-line"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_HIGHLIGHT_MATCHING_BRACKETS, "highlight-matching-brackets"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_IMPLICIT_TRAILING_NEWLINE, "implicit-trailing-newline"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_INDENT_WIDTH, "indent-width"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_INSERT_SPACES_INSTEAD_OF_TABS, "insert-spaces-instead-of-tabs"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_KEYBINDINGS, "keybindings"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_LANGUAGE, "language"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_LINE_HEIGHT, "line-height"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_NEWLINE_TYPE, "newline-type"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_RIGHT_MARGIN_POSITION, "right-margin-position"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_SHOW_LINE_NUMBERS, "show-line-numbers"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_SHOW_RIGHT_MARGIN, "show-right-margin"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_SYNTAX, "syntax"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_TAB_WIDTH, "tab-width"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_WRAP_TEXT, "wrap-text"),
                    G_DEFINE_ENUM_VALUE (EDITOR_SETTINGS_OPTION_ZOOM, "zoom"))
