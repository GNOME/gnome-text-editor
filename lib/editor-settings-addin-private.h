/*
 * editor-settings-addin-private.h
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

#include "editor-types.h"

G_BEGIN_DECLS

typedef enum _EditorSettingsOption
{
  /* Boolean Types */
  EDITOR_SETTINGS_OPTION_AUTO_INDENT,
  EDITOR_SETTINGS_OPTION_DISCOVER_SETTINGS,
  EDITOR_SETTINGS_OPTION_ENABLE_SNIPPETS,
  EDITOR_SETTINGS_OPTION_ENABLE_SPELLCHECK,
  EDITOR_SETTINGS_OPTION_HIGHLIGHT_CURRENT_LINE,
  EDITOR_SETTINGS_OPTION_HIGHLIGHT_MATCHING_BRACKETS,
  EDITOR_SETTINGS_OPTION_IMPLICIT_TRAILING_NEWLINE,
  EDITOR_SETTINGS_OPTION_INSERT_SPACES_INSTEAD_OF_TABS,
  EDITOR_SETTINGS_OPTION_SHOW_LINE_NUMBERS,
  EDITOR_SETTINGS_OPTION_SHOW_RIGHT_MARGIN,
  EDITOR_SETTINGS_OPTION_WRAP_TEXT,

  /* Unsigned Integer Types */
  EDITOR_SETTINGS_OPTION_RIGHT_MARGIN_POSITION,
  EDITOR_SETTINGS_OPTION_TAB_WIDTH,

  /* Integer Types */
  EDITOR_SETTINGS_OPTION_INDENT_WIDTH,

  /* String Types */
  EDITOR_SETTINGS_OPTION_ENCODING,
  EDITOR_SETTINGS_OPTION_LANGUAGE,
  EDITOR_SETTINGS_OPTION_SYNTAX,

  EDITOR_SETTINGS_OPTION_KEYBINDINGS,

  /* Enum Types */
  EDITOR_SETTINGS_OPTION_NEWLINE_TYPE,
  EDITOR_SETTINGS_OPTION_ZOOM,

  /* Object Types */
  EDITOR_SETTINGS_OPTION_DRAW_SPACES,

  /* Double Types */
  EDITOR_SETTINGS_OPTION_LINE_HEIGHT,
} EditorSettingsOption;

#define EDITOR_TYPE_SETTINGS_ADDIN  (editor_settings_addin_get_type())
#define EDITOR_TYPE_SETTINGS_OPTION (editor_settings_option_get_type())

G_DECLARE_DERIVABLE_TYPE (EditorSettingsAddin, editor_settings_addin, EDITOR, SETTINGS_ADDIN, GObject)

struct _EditorSettingsAddinClass
{
  GObjectClass parent_class;

  DexFuture *(*load)      (EditorSettingsAddin  *self,
                           EditorDocument       *document,
                           GFile                *file);
  gboolean   (*get_value) (EditorSettingsAddin  *self,
                           EditorSettingsOption  option,
                           GValue               *value);
  void       (*changed)   (EditorSettingsAddin  *self,
                           EditorSettingsOption  option);
};

GType      editor_settings_option_get_type (void) G_GNUC_CONST;
DexFuture *editor_settings_addin_load      (EditorSettingsAddin  *self,
                                            EditorDocument       *document,
                                            GFile                *file);
gboolean   editor_settings_addin_get_value (EditorSettingsAddin  *self,
                                            EditorSettingsOption  option,
                                            GValue               *value);
void       editor_settings_addin_changed   (EditorSettingsAddin  *self,
                                            EditorSettingsOption  option);

G_END_DECLS
