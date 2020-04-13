/* editor-session-private.h
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

#include "editor-session.h"

G_BEGIN_DECLS

EditorSession *_editor_session_new                    (void);
EditorWindow  *_editor_session_create_window_no_draft (EditorSession  *self);
gboolean       _editor_session_did_restore            (EditorSession  *self);
GPtrArray     *_editor_session_get_pages              (EditorSession  *self);
void           _editor_session_document_seen          (EditorSession  *self,
                                                       EditorDocument *document);

G_END_DECLS
