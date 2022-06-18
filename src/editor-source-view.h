/* editor-source-view.h
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
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

#define EDITOR_TYPE_SOURCE_VIEW (editor_source_view_get_type())

G_DECLARE_FINAL_TYPE (EditorSourceView, editor_source_view, EDITOR, SOURCE_VIEW, GtkSourceView)

const PangoFontDescription *editor_source_view_get_font_desc      (EditorSourceView           *self);
void                        editor_source_view_set_font_desc      (EditorSourceView           *self,
                                                                   const PangoFontDescription *font_desc);
double                      editor_source_view_get_zoom_level     (EditorSourceView           *self);
void                        editor_source_view_prepend_extra_menu (EditorSourceView           *self,
                                                                   GMenuModel                 *extra_menu);
void                        editor_source_view_jump_to_iter       (GtkTextView                *text_view,
                                                                   const GtkTextIter          *iter,
                                                                   double                      within_margin,
                                                                   gboolean                    use_align,
                                                                   double                      xalign,
                                                                   double                      yalign);

G_END_DECLS
