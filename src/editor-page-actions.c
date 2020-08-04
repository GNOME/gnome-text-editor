/* editor-page-actions.c
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

#define G_LOG_DOMAIN "editor-page-actions"

#include "editor-page-private.h"
#include "editor-language-dialog.h"

static void
editor_page_actions_language (GSimpleAction *action,
                              GVariant      *param,
                              gpointer       user_data)
{
  EditorPage *self = user_data;
  EditorLanguageDialog *dialog;

  g_assert (EDITOR_IS_PAGE (self));

  dialog = editor_language_dialog_new (NULL);
  g_object_bind_property (dialog, "language",
                          self, "language",
                          (G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL));
  gtk_window_present (GTK_WINDOW (dialog));
}

static void
editor_page_actions_search_hide (GSimpleAction *action,
                                 GVariant      *param,
                                 gpointer       user_data)
{
  EditorPage *self = user_data;

  g_assert (EDITOR_IS_PAGE (self));

  _editor_page_hide_search (self);
  gtk_widget_grab_focus (GTK_WIDGET (self));
}

static void
editor_page_actions_search_move_next (GSimpleAction *action,
                                      GVariant      *param,
                                      gpointer       user_data)
{
  EditorPage *self = user_data;

  g_assert (EDITOR_IS_PAGE (self));

  _editor_page_move_next_search (self);
}

static void
editor_page_actions_search_move_previous (GSimpleAction *action,
                                          GVariant      *param,
                                          gpointer       user_data)
{
  EditorPage *self = user_data;

  g_assert (EDITOR_IS_PAGE (self));

  _editor_page_move_previous_search (self);
}

static void
editor_page_actions_replace_one (GSimpleAction *action,
                                 GVariant      *param,
                                 gpointer       user_data)
{
  EditorPage *self = user_data;

  g_assert (EDITOR_IS_PAGE (self));

  if (_editor_search_bar_get_can_replace (self->search_bar))
    {
      _editor_search_bar_replace (self->search_bar);
      _editor_page_scroll_to_insert (self);
      _editor_search_bar_move_next (self->search_bar);
    }
}

static void
editor_page_actions_replace_all (GSimpleAction *action,
                                 GVariant      *param,
                                 gpointer       user_data)
{
  EditorPage *self = user_data;

  g_assert (EDITOR_IS_PAGE (self));

  if (_editor_search_bar_get_can_replace_all (self->search_bar))
    {
      _editor_search_bar_replace_all (self->search_bar);
      _editor_page_scroll_to_insert (self);
    }
}

static void
on_notify_can_move_cb (EditorPage      *self,
                       GParamSpec      *pspec,
                       EditorSearchBar *search_bar)
{
  GActionGroup *search;
  GAction *action;
  gboolean can_move;

  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_SEARCH_BAR (search_bar));

  search = gtk_widget_get_action_group (GTK_WIDGET (self), "search");
  can_move = _editor_search_bar_get_can_move (search_bar);

  action = g_action_map_lookup_action (G_ACTION_MAP (search), "move-next");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), can_move);

  action = g_action_map_lookup_action (G_ACTION_MAP (search), "move-previous");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action), can_move);
}

static void
on_notify_can_replace_cb (EditorPage      *self,
                          GParamSpec      *pspec,
                          EditorSearchBar *search_bar)
{
  GActionGroup *search;
  GAction *action;

  g_assert (EDITOR_IS_PAGE (self));
  g_assert (EDITOR_IS_SEARCH_BAR (search_bar));

  search = gtk_widget_get_action_group (GTK_WIDGET (self), "search");

  action = g_action_map_lookup_action (G_ACTION_MAP (search), "replace-one");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
                               _editor_search_bar_get_can_replace (search_bar));

  action = g_action_map_lookup_action (G_ACTION_MAP (search), "replace-all");
  g_simple_action_set_enabled (G_SIMPLE_ACTION (action),
                               _editor_search_bar_get_can_replace_all (search_bar));
}

void
_editor_page_class_actions_init (EditorPageClass *klass)
{
}

void
_editor_page_actions_init (EditorPage *self)
{
  static const GActionEntry page_actions[] = {
    { "language", editor_page_actions_language, "s" },
  };
  static const GActionEntry search_actions[] = {
    { "hide", editor_page_actions_search_hide },
    { "move-next", editor_page_actions_search_move_next },
    { "move-previous", editor_page_actions_search_move_previous },
    { "replace-one", editor_page_actions_replace_one },
    { "replace-all", editor_page_actions_replace_all },
  };

  g_autoptr(GSimpleActionGroup) page = g_simple_action_group_new ();
  g_autoptr(GSimpleActionGroup) search = g_simple_action_group_new ();

  g_action_map_add_action_entries (G_ACTION_MAP (page), page_actions, G_N_ELEMENTS (page_actions), self);
  g_action_map_add_action_entries (G_ACTION_MAP (search), search_actions, G_N_ELEMENTS (search_actions), self);

  gtk_widget_insert_action_group (GTK_WIDGET (self), "page", G_ACTION_GROUP (page));
  gtk_widget_insert_action_group (GTK_WIDGET (self), "search", G_ACTION_GROUP (search));

  g_signal_connect_object (self->search_bar,
                           "notify::can-move",
                           G_CALLBACK (on_notify_can_move_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->search_bar,
                           "notify::can-replace",
                           G_CALLBACK (on_notify_can_replace_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->search_bar,
                           "notify::can-replace-all",
                           G_CALLBACK (on_notify_can_replace_cb),
                           self,
                           G_CONNECT_SWAPPED);
}
