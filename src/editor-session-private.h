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
#include "editor-sidebar-model-private.h"

G_BEGIN_DECLS

typedef struct
{
  gchar *draft_id;
  gchar *title;
  gchar *uri;
} EditorSessionDraft;

struct _EditorSession
{
  GObject             parent_instance;

  GPtrArray          *windows;
  GPtrArray          *pages;
  GFile              *state_file;
  GHashTable         *seen;
  GHashTable         *forgot;
  GArray             *drafts;
  EditorSidebarModel *recents;
  GSettings          *privacy;

  guint               auto_save_delay;
  guint               auto_save_source;


  guint               auto_save : 1;
  guint               did_restore : 1;
  guint               restore_pages : 1;
  guint               dirty : 1;
  guint               can_clear_history : 1;
};

EditorSession *_editor_session_new                    (void);
EditorWindow  *_editor_session_create_window_no_draft (EditorSession  *self);
gboolean       _editor_session_did_restore            (EditorSession  *self);
GPtrArray     *_editor_session_get_pages              (EditorSession  *self);
void           _editor_session_document_seen          (EditorSession  *self,
                                                       EditorDocument *document);
GArray        *_editor_session_get_drafts             (EditorSession  *self);
void           _editor_session_add_draft              (EditorSession  *self,
                                                       const gchar    *draft_id,
                                                       const gchar    *title,
                                                       const gchar    *uri);
void           _editor_session_remove_window          (EditorSession  *self,
                                                       EditorWindow   *window);
void           _editor_session_remove_draft           (EditorSession  *self,
                                                       const gchar    *draft_id);
EditorPage    *_editor_session_open_draft             (EditorSession  *self,
                                                       EditorWindow   *window,
                                                       const gchar    *draft_id);
void           _editor_session_move_page_to_window    (EditorSession  *session,
                                                       EditorPage     *page,
                                                       EditorWindow   *window);
void           _editor_session_forget                 (EditorSession  *self,
                                                       GFile          *file,
                                                       const gchar    *draft_id);
void           _editor_session_mark_dirty             (EditorSession  *self);
void           _editor_session_set_restore_pages      (EditorSession  *self,
                                                       gboolean        restore_pages);
void           _editor_session_clear_history          (EditorSession  *self);

G_END_DECLS
