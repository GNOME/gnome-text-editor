/* editor-types.h
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

#include <gio/gio.h>
#include <gtk/gtk.h>
#include <adwaita.h>
#include <gtksourceview/gtksource.h>

G_BEGIN_DECLS

typedef struct _EditorApplication          EditorApplication;
typedef struct _EditorDocument             EditorDocument;
typedef struct _EditorDocumentStatistics   EditorDocumentStatistics;
typedef struct _EditorEncoding             EditorEncoding;
typedef struct _EditorLanguageDialog       EditorLanguageDialog;
typedef struct _EditorPage                 EditorPage;
typedef struct _EditorPageSettings         EditorPageSettings;
typedef struct _EditorPageSettingsProvider EditorPageSettingsProvider;
typedef struct _EditorPreferencesRadio     EditorPreferencesRadio;
typedef struct _EditorPreferencesSpin      EditorPreferencesSpin;
typedef struct _EditorPreferencesSwitch    EditorPreferencesSwitch;
typedef struct _EditorPreferencesWindow    EditorPreferencesWindow;
typedef struct _EditorSession              EditorSession;
typedef struct _EditorTab                  EditorTab;
typedef struct _EditorWindow               EditorWindow;

typedef enum
{
  EDITOR_INDENT_STYLE_TAB = 0,
  EDITOR_INDENT_STYLE_SPACE,
} EditorIndentStyle;

G_END_DECLS
