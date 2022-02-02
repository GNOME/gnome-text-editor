/* editor-application.h
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

#include "editor-types.h"

G_BEGIN_DECLS

#define EDITOR_TYPE_APPLICATION    (editor_application_get_type())
#define EDITOR_APPLICATION_DEFAULT (EDITOR_APPLICATION(g_application_get_default()))

G_DECLARE_FINAL_TYPE (EditorApplication, editor_application, EDITOR, APPLICATION, AdwApplication)

EditorSession *editor_application_get_session            (EditorApplication    *self);
EditorWindow  *editor_application_get_current_window     (EditorApplication    *self);
const char    *editor_application_get_style_scheme       (EditorApplication    *self);
void           editor_application_set_style_scheme       (EditorApplication    *self,
                                                          const char           *style_scheme);
void           editor_application_install_schemes_async  (EditorApplication    *self,
                                                          GFile               **files,
                                                          guint                 n_files,
                                                          GCancellable         *cancellable,
                                                          GAsyncReadyCallback   callback,
                                                          gpointer              user_data);
gboolean       editor_application_install_schemes_finish (EditorApplication     *self,
                                                          GAsyncResult          *result,
                                                          GError               **error);

G_END_DECLS
