/* editor-tab-actions.c
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

#define G_LOG_DOMAIN "editor-tab-actions"

#include "config.h"

#include "editor-session-private.h"
#include "editor-tab-private.h"
#include "editor-utils-private.h"
#include "editor-window-private.h"

static void
editor_tab_actions_close (GSimpleAction *action,
                          GVariant      *param,
                          gpointer       user_data)
{
  EditorTab *self = user_data;

  g_assert (EDITOR_IS_TAB (self));

  if (self->page != NULL)
    editor_session_remove_page (EDITOR_SESSION_DEFAULT, self->page);
}

static void
editor_tab_actions_close_other_tabs (GSimpleAction *action,
                                     GVariant      *param,
                                     gpointer       user_data)
{
  EditorTab *self = user_data;
  EditorWindow *window;
  GList *pages;

  g_assert (EDITOR_IS_TAB (self));

  if (self->page == NULL)
    return;

  window = _editor_page_get_window (self->page);
  pages = _editor_window_get_pages (window);

  for (const GList *iter = pages; iter; iter = iter->next)
    {
      EditorPage *page = iter->data;

      if (page == self->page)
        continue;

      editor_session_remove_page (EDITOR_SESSION_DEFAULT, page);
    }

  g_list_free (pages);
}

static void
editor_tab_actions_move_to_new_window (GSimpleAction *action,
                                       GVariant      *param,
                                       gpointer       user_data)
{
  EditorTab *self = user_data;
  EditorWindow *window;

  g_assert (EDITOR_IS_TAB (self));

  if (self->page == NULL)
    return;

  window = _editor_session_create_window_no_draft (EDITOR_SESSION_DEFAULT);
  _editor_session_move_page_to_window (EDITOR_SESSION_DEFAULT,
                                       self->page,
                                       window);
  gtk_window_present (GTK_WINDOW (window));
}

static void
editor_tab_actions_move_left (GSimpleAction *action,
                              GVariant      *param,
                              gpointer       user_data)
{
  EditorTab *self = user_data;
  GtkNotebook *notebook;
  gint page_num;

  g_assert (EDITOR_IS_TAB (self));

  if (self->page == NULL)
    return;

  notebook = GTK_NOTEBOOK (gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_NOTEBOOK));

  if (notebook == NULL)
    return;

  page_num = gtk_notebook_page_num (notebook, GTK_WIDGET (self->page));

  if (page_num > 0)
    {
      gtk_notebook_reorder_child (notebook,
                                  GTK_WIDGET (self->page),
                                  page_num - 1);
      _editor_page_raise (self->page);
    }
}

static void
editor_tab_actions_move_right (GSimpleAction *action,
                               GVariant      *param,
                               gpointer       user_data)
{
  EditorTab *self = user_data;
  GtkNotebook *notebook;
  gint page_num;

  g_assert (EDITOR_IS_TAB (self));

  if (self->page == NULL)
    return;

  notebook = GTK_NOTEBOOK (gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_NOTEBOOK));

  if (notebook == NULL)
    return;

  page_num = gtk_notebook_page_num (notebook, GTK_WIDGET (self->page));
  gtk_notebook_reorder_child (notebook,
                              GTK_WIDGET (self->page),
                              page_num + 1);
  _editor_page_raise (self->page);
}

void
_editor_tab_class_actions_init (EditorTabClass *klass)
{
}

void
_editor_tab_actions_init (EditorTab *self)
{
  static const GActionEntry tab_actions[] = {
    { "close", editor_tab_actions_close },
    { "close-other-tabs", editor_tab_actions_close_other_tabs },
    { "move-to-new-window", editor_tab_actions_move_to_new_window },
    { "move-left", editor_tab_actions_move_left },
    { "move-right", editor_tab_actions_move_right },
  };

  g_autoptr(GSimpleActionGroup) tab = g_simple_action_group_new ();

  g_action_map_add_action_entries (G_ACTION_MAP (tab),
                                   tab_actions,
                                   G_N_ELEMENTS (tab_actions),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self), "tab", G_ACTION_GROUP (tab));
}

void
_editor_tab_actions_update (EditorTab *self)
{
  GtkWidget *notebook;
  gboolean move_left = FALSE;
  gboolean move_right = FALSE;

  g_assert (EDITOR_IS_TAB (self));

  notebook = gtk_widget_get_ancestor (GTK_WIDGET (self), GTK_TYPE_NOTEBOOK);

  if (notebook != NULL && self->page != NULL)
    {
      gint n_pages = gtk_notebook_get_n_pages (GTK_NOTEBOOK (notebook));
      gint page_num = gtk_notebook_page_num (GTK_NOTEBOOK (notebook), GTK_WIDGET (self->page));

      move_left = page_num > 0;
      move_right = page_num < (n_pages - 1);
    }

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "tab.move-left", move_left);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "tab.move-right", move_right);
}

