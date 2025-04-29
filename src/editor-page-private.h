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
#include "editor-document-private.h"
#include "editor-page.h"
#include "editor-page-settings.h"
#include "editor-path-private.h"
#include "editor-position-label-private.h"
#include "editor-print-operation.h"
#include "editor-search-bar-private.h"
#include "editor-utils-private.h"
#include "editor-window.h"

G_BEGIN_DECLS

struct _EditorPage
{
  GtkWidget                parent_instance;

  GCancellable            *cancellable;

  EditorDocument          *document;
  EditorPageSettings      *settings;
  GBindingGroup           *settings_bindings;

  EditorAnimation         *progress_animation;

  GtkWidget               *box;
  GtkOverlay              *overlay;
  GtkScrolledWindow       *scroller;
  GtkSourceView           *view;
  GtkSourceMap            *map;
  GtkProgressBar          *progress_bar;
  GtkWidget               *goto_line_revealer;
  GtkEntry                *goto_line_entry;
  AdwToolbarView          *toolbar_view;
  GtkWidget               *search_revealer;
  EditorSearchBar         *search_bar;
  GtkInfoBar              *changed_infobar;
  GtkInfoBar              *infobar;
  GtkEventController      *vim;
  EditorPositionLabel     *position_label;

  guint                    cached_line;
  guint                    cached_visual_column;

  guint                    queued_hide_position;

  guint                    close_requested : 1;
  guint                    moving : 1;
};

void          _editor_page_class_actions_init     (EditorPageClass      *klass);
void          _editor_page_actions_init           (EditorPage           *self);
void          _editor_page_actions_bind_settings  (EditorPage           *self,
                                                   EditorPageSettings   *settings);
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
void          _editor_page_vim_init               (EditorPage           *self);
void          _editor_page_zoom_in                (EditorPage           *self);
void          _editor_page_zoom_out               (EditorPage           *self);
void          _editor_page_zoom_one               (EditorPage           *self);
char         *_editor_page_get_zoom_label         (EditorPage           *self);
GCancellable *_editor_page_get_cancellable        (EditorPage           *self);

G_END_DECLS
