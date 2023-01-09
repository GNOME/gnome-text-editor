/* editor-application-private.h
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

#include "editor-application.h"

G_BEGIN_DECLS

struct _EditorApplication
{
  AdwApplication  parent_instance;
  EditorSession  *session;
  GSettings      *settings;
  GtkCssProvider *recoloring;
  GDBusProxy     *portal;
  char           *system_font_name;
  GHashTable     *open_at_position;
  guint           standalone : 1;
};

EditorApplication    *_editor_application_new              (gboolean           standalone);
void                  _editor_application_actions_init     (EditorApplication *self);
PangoFontDescription *_editor_application_get_system_font  (EditorApplication *self);
gboolean              _editor_application_consume_position (EditorApplication *self,
                                                            GFile             *file,
                                                            guint             *line,
                                                            guint             *line_offset);

G_END_DECLS
