/* editor-info-bar-private.h
 *
 * Copyright 2021 Christian Hergert <unknown@domain.org>
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

#include <gtk/gtk.h>

#include "editor-types.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_INFO_BAR (editor_info_bar_get_type())

G_DECLARE_FINAL_TYPE (EditorInfoBar, editor_info_bar, EDITOR, INFO_BAR, GtkWidget)

GtkWidget *_editor_info_bar_new (EditorDocument *document);

G_END_DECLS
