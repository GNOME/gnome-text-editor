/* editor-window-private.h
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

#include "editor-window.h"
#include "editor-position-label-private.h"
#include "editor-sidebar-private.h"
#include "editor-page-private.h"
#include "editor-signal-group.h"
#include "editor-binding-group.h"

G_BEGIN_DECLS

struct _EditorWindow
{
  HdyApplicationWindow  parent_instance;

  /* Template Widgets */
  GtkWidget            *drop_box;
  GtkWidget            *empty;
  GtkNotebook          *notebook;
  GtkLabel             *title;
  GtkLabel             *subtitle;
  GtkLabel             *is_modified;
  GtkBox               *position_box;
  EditorPositionLabel  *position_label;
  GtkPaned             *paned;
  GtkStack             *stack;
  GtkToggleButton      *open_toggle_button;
  EditorSidebar        *sidebar;
  GtkMenuButton        *primary_menu;
  GtkMenuButton        *options_menu;
  GtkMenuButton        *export_menu;

  /* Borrowed References */
  EditorPage           *visible_page;

  /* Owned References */
  EditorBindingGroup   *page_bindings;
  EditorSignalGroup    *page_signals;
  EditorSignalGroup    *document_signals;
  GSettings            *settings;
};


void          _editor_window_class_actions_init   (EditorWindowClass *klass);
void          _editor_window_actions_init         (EditorWindow      *self);
void          _editor_window_actions_update       (EditorWindow      *self,
                                                   EditorPage        *page);
void          _editor_window_dnd_init             (EditorWindow      *self);
EditorWindow *_editor_window_new                  (void);
gboolean      _editor_window_get_sidebar_revealed (EditorWindow      *self);
void          _editor_window_set_sidebar_revealed (EditorWindow      *self,
                                                   gboolean           sidebar_revealed);
GList        *_editor_window_get_pages            (EditorWindow      *self);
void          _editor_window_add_page             (EditorWindow      *self,
                                                   EditorPage        *page);
void          _editor_window_remove_page          (EditorWindow      *self,
                                                   EditorPage        *page);
void          _editor_window_focus_search         (EditorWindow      *self);

G_END_DECLS
