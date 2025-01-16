/* editor-fullscreen-box.h
 *
 * Copyright © 2021 Purism SPC
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

#define EDITOR_TYPE_FULLSCREEN_BOX (editor_fullscreen_box_get_type())

G_DECLARE_FINAL_TYPE (EditorFullscreenBox, editor_fullscreen_box, EDITOR, FULLSCREEN_BOX, GtkWidget)

EditorFullscreenBox *editor_fullscreen_box_new            (void);
gboolean             editor_fullscreen_box_get_fullscreen (EditorFullscreenBox *self);
void                 editor_fullscreen_box_set_fullscreen (EditorFullscreenBox *self,
                                                           gboolean             fullscreen);
gboolean             editor_fullscreen_box_get_autohide   (EditorFullscreenBox *self);
void                 editor_fullscreen_box_set_autohide   (EditorFullscreenBox *self,
                                                           gboolean             autohide);
GtkWidget           *editor_fullscreen_box_get_content    (EditorFullscreenBox *self);
void                 editor_fullscreen_box_set_content    (EditorFullscreenBox *self,
                                                           GtkWidget           *content);
void                 editor_fullscreen_box_add_top_bar    (EditorFullscreenBox *self,
                                                           GtkWidget           *child);
void                 editor_fullscreen_box_add_bottom_bar (EditorFullscreenBox *self,
                                                           GtkWidget           *child);
void                 editor_fullscreen_box_reveal         (EditorFullscreenBox *self);

G_END_DECLS
