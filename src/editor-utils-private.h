/* editor-utils-private.h
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

#include <gtksourceview/gtksource.h>

#include "editor-types.h"

G_BEGIN_DECLS

char                    *_editor_font_description_to_css        (const PangoFontDescription *font_desc);
void                     _editor_widget_hide_with_fade          (GtkWidget                  *widget);
void                     _editor_widget_hide_with_fade_delay    (GtkWidget                  *widget,
                                                                 guint                       delay);
gboolean                 _editor_gchararray_to_boolean          (GBinding                   *binding,
                                                                 const GValue               *from_value,
                                                                 GValue                     *to_value,
                                                                 gpointer                    user_data);
gboolean                 _editor_gboolean_to_wrap_mode          (GBinding                   *binding,
                                                                 const GValue               *from_value,
                                                                 GValue                     *to_value,
                                                                 gpointer                    user_data);
gboolean                 _editor_gboolean_to_background_pattern (GBinding                   *binding,
                                                                 const GValue               *from_value,
                                                                 GValue                     *to_value,
                                                                 gpointer                    user_data);
gboolean                 _editor_gboolean_to_scroll_policy      (GBinding                   *binding,
                                                                 const GValue               *from_value,
                                                                 GValue                     *to_value,
                                                                 gpointer                    user_data);
gboolean                 _editor_gchararray_to_style_scheme     (GBinding                   *binding,
                                                                 const GValue               *from_value,
                                                                 GValue                     *to_value,
                                                                 gpointer                    user_data);
char                    *_editor_date_time_format               (GDateTime                  *self);
void                     _editor_revealer_auto_hide             (GtkRevealer                *revealer);

G_END_DECLS
