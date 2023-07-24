/* editor-language-row-private.h
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

#include <adwaita.h>

#include "editor-types-private.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_LANGUAGE_ROW (editor_language_row_get_type())

G_DECLARE_FINAL_TYPE (EditorLanguageRow, editor_language_row, EDITOR, LANGUAGE_ROW, AdwActionRow)

GtkWidget         *_editor_language_row_new          (GtkSourceLanguage *language);
GtkSourceLanguage *_editor_language_row_get_language (EditorLanguageRow *self);
void               _editor_language_row_set_selected (EditorLanguageRow *self,
                                                      gboolean           selected);
gboolean           _editor_language_row_match        (EditorLanguageRow *self,
                                                      GPatternSpec      *spec);

G_END_DECLS
