/* editor-page-private.h
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

#include "editor-animation.h"
#include "editor-binding-group.h"
#include "editor-document-private.h"
#include "editor-page.h"
#include "editor-page-settings.h"
#include "editor-path-private.h"
#include "editor-print-operation.h"
#include "editor-search-bar-private.h"
#include "editor-utils-private.h"
#include "editor-window.h"

G_BEGIN_DECLS

struct _EditorPage
{
  GtkWidget                parent_instance;

  EditorDocument          *document;
  EditorPageSettings      *settings;
  EditorBindingGroup      *settings_bindings;

  EditorAnimation         *progress_animation;

  GtkWidget               *box;
  GtkOverlay              *overlay;
  GtkScrolledWindow       *scroller;
  GtkSourceView           *view;
  GtkSourceMap            *map;
  GtkProgressBar          *progress_bar;
  GtkRevealer             *goto_line_revealer;
  GtkEntry                *goto_line_entry;
  GtkRevealer             *search_revealer;
  EditorSearchBar         *search_bar;
  GtkInfoBar              *changed_infobar;
  GtkInfoBar              *infobar;

  guint                    close_requested : 1;
  guint                    moving : 1;
};

void          _editor_page_class_actions_init     (EditorPageClass      *klass);
void          _editor_page_actions_init           (EditorPage           *self);
EditorWindow *_editor_page_get_window             (EditorPage           *self);
void          _editor_page_save                   (EditorPage           *self);
void          _editor_page_save_as                (EditorPage           *self,
                                                   const char           *filename);
void          _editor_page_raise                  (EditorPage           *self);
void          _editor_page_discard_changes_async  (EditorPage           *self,
                                                   gboolean              reload,
                                                   GCancellable         *cancellable,
                                                   GAsyncReadyCallback   callback,
                                                   gpointer              user_data);
gboolean      _editor_page_discard_changes_finish (EditorPage           *self,
                                                   GAsyncResult         *result,
                                                   GError              **error);
void          _editor_page_discard_changes        (EditorPage           *self);
void          _editor_page_print                  (EditorPage           *self);
void          _editor_page_copy_all               (EditorPage           *self);
void          _editor_page_discard_changes        (EditorPage           *self);
gint          _editor_page_position               (EditorPage           *self);
gchar        *_editor_page_dup_title_no_i18n      (EditorPage           *self);
void          _editor_page_begin_search           (EditorPage           *self);
void          _editor_page_begin_replace          (EditorPage           *self);
void          _editor_page_hide_search            (EditorPage           *self);
void          _editor_page_scroll_to_insert       (EditorPage           *self);
void          _editor_page_move_next_search       (EditorPage           *self,
                                                   gboolean              hide_after_search);
void          _editor_page_move_previous_search   (EditorPage           *self,
                                                   gboolean              hide_after_search);
void          _editor_page_begin_move             (EditorPage           *self);
void          _editor_page_end_move               (EditorPage           *self);

G_END_DECLS
