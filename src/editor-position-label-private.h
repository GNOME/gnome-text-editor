/* editor-position-label-private.h
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

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define EDITOR_TYPE_POSITION_LABEL (editor_position_label_get_type())

G_DECLARE_FINAL_TYPE (EditorPositionLabel, editor_position_label, EDITOR, POSITION_LABEL, GtkWidget)

EditorPositionLabel *_editor_position_label_new          (void);
void                 _editor_position_label_set_position (EditorPositionLabel *self,
                                                          guint                line,
                                                          guint                column);

G_END_DECLS
