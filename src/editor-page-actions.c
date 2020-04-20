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
editor_page_actions_language (GtkWidget   *widget,
                              const gchar *action_name,
                              GVariant    *param)
{
  EditorPage *page = (EditorPage *)widget;
  EditorLanguageDialog *dialog;

  g_assert (EDITOR_IS_PAGE (page));
  g_assert (g_str_equal (action_name, "page.language"));

  dialog = editor_language_dialog_new (NULL);
  g_object_bind_property (dialog, "language",
                          page, "language",
                          (G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL));
  gtk_window_present (GTK_WINDOW (dialog));
}

static void
editor_page_actions_search_hide (GtkWidget   *widget,
                                 const gchar *action_name,
                                 GVariant    *param)
{
  _editor_page_set_search_visible (EDITOR_PAGE (widget), FALSE, 0);
}

static void
editor_page_actions_begin_search (GtkWidget   *widget,
                                  const gchar *action_name,
                                  GVariant    *param)
{
  _editor_page_set_search_visible (EDITOR_PAGE (widget),
                                   TRUE,
                                   EDITOR_SEARCH_BAR_MODE_SEARCH);
}

static void
editor_page_actions_begin_replace (GtkWidget   *widget,
                                   const gchar *action_name,
                                   GVariant    *param)
{
  _editor_page_set_search_visible (EDITOR_PAGE (widget),
                                   TRUE,
                                   EDITOR_SEARCH_BAR_MODE_REPLACE);
}

void
_editor_page_class_actions_init (EditorPageClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_install_action (widget_class, "page.language", NULL, editor_page_actions_language);
  gtk_widget_class_install_action (widget_class, "search.hide", NULL, editor_page_actions_search_hide);
  gtk_widget_class_install_action (widget_class, "search.begin-search", NULL, editor_page_actions_begin_search);
  gtk_widget_class_install_action (widget_class, "search.begin-replace", NULL, editor_page_actions_begin_replace);
}

void
_editor_page_actions_init (EditorPage *self)
{
}
