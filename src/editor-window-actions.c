/* editor-window-actions.c
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

#define G_LOG_DOMAIN "editor-window-actions"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-application.h"
#include "editor-document.h"
#include "editor-language-dialog.h"
#include "editor-page-private.h"
#include "editor-session-private.h"
#include "editor-window-private.h"

static void
editor_window_actions_new_draft_cb (GSimpleAction *action,
                                    GVariant      *param,
                                    gpointer       user_data)
{
  EditorWindow *self = user_data;
  EditorSession *session;

  g_assert (EDITOR_IS_WINDOW (self));

  session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);
  editor_session_add_draft (session, self);
}

static void
editor_window_actions_close_page_cb (GSimpleAction *action,
                                     GVariant      *param,
                                     gpointer       user_data)
{
  EditorWindow *self = user_data;
  EditorSession *session;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);

  page = editor_window_get_visible_page (self);
  if (page != NULL)
    editor_session_remove_page (session, page);

  page = editor_window_get_visible_page (self);
  if (page == NULL)
    gtk_window_close (GTK_WINDOW (self));
}

static void
editor_window_actions_save_cb (GSimpleAction *action,
                               GVariant      *param,
                               gpointer       user_data)
{
  EditorWindow *self = user_data;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  page = editor_window_get_visible_page (self);

  if (page != NULL)
    _editor_page_save (page);
}

static void
editor_window_actions_save_as_cb (GSimpleAction *action,
                                  GVariant      *param,
                                  gpointer       user_data)
{
  EditorWindow *self = user_data;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  page = editor_window_get_visible_page (self);

  if (page != NULL)
    _editor_page_save_as (page);
}

static void
editor_window_actions_change_language_cb (GSimpleAction *action,
                                          GVariant      *param,
                                          gpointer       user_data)
{
  EditorWindow *self = user_data;
  EditorLanguageDialog *dialog;
  EditorDocument *document;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if (!(page = editor_window_get_visible_page (self)))
    return;

  document = editor_page_get_document (page);

  dialog = editor_language_dialog_new (EDITOR_APPLICATION_DEFAULT);
  gtk_window_set_transient_for (GTK_WINDOW (dialog), GTK_WINDOW (self));
  gtk_window_set_destroy_with_parent (GTK_WINDOW (dialog), TRUE);
  gtk_window_set_modal (GTK_WINDOW (dialog), FALSE);

  g_object_bind_property (document, "language", dialog, "language",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  gtk_window_present (GTK_WINDOW (dialog));
}

static void
editor_window_actions_discard_changes_cb (GSimpleAction *action,
                                          GVariant      *param,
                                          gpointer       user_data)
{
  EditorWindow *self = user_data;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if ((page = editor_window_get_visible_page (self)))
    _editor_page_discard_changes (page);
}

static void
editor_window_actions_print_cb (GSimpleAction *action,
                                GVariant      *param,
                                gpointer       user_data)
{
  EditorWindow *self = user_data;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if ((page = editor_window_get_visible_page (self)))
    _editor_page_print (page);
}

static void
editor_window_actions_change_page_cb (GSimpleAction *action,
                                      GVariant      *param,
                                      gpointer       user_data)
{
  EditorWindow *self = user_data;
  EditorPage *page;
  gint page_num;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_INT32));

  page_num = g_variant_get_int32 (param) - 1;
  if (page_num < 0)
    return;

  if ((page = editor_window_get_nth_page (self, page_num)))
    _editor_page_raise (page);
}

static void
editor_window_actions_copy_all_cb (GSimpleAction *action,
                                   GVariant      *param,
                                   gpointer       user_data)
{
  EditorWindow *self = user_data;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if ((page = editor_window_get_visible_page (self)))
    _editor_page_copy_all (page);
}

static void
editor_window_actions_open_response_cb (EditorWindow         *self,
                                        gint                  response_id,
                                        GtkFileChooserNative *native)
{
  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (GTK_IS_FILE_CHOOSER_NATIVE (native));

  if (response_id == GTK_RESPONSE_ACCEPT)
    {
      g_autoptr(GFile) file = gtk_file_chooser_get_file (GTK_FILE_CHOOSER (native));

      editor_session_open (EDITOR_SESSION_DEFAULT, self, file);
    }

  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (native));
}

static void
editor_window_actions_open_cb (GSimpleAction *action,
                               GVariant      *param,
                               gpointer       user_data)
{
  EditorWindow *self = user_data;
  g_autoptr(GtkFileFilter) all_files = NULL;
  g_autoptr(GtkFileFilter) text_files = NULL;
  GtkFileChooserNative *native;
  EditorDocument *document;
  EditorPage *page;
  GFile *dfile;

  g_assert (EDITOR_IS_WINDOW (self));

  native = gtk_file_chooser_native_new (_("Open File"),
                                        GTK_WINDOW (self),
                                        GTK_FILE_CHOOSER_ACTION_OPEN,
                                        _("Open"),
                                        _("Cancel"));

  if ((page = editor_window_get_visible_page (self)) &&
      (document = editor_page_get_document (page)) &&
      (dfile = editor_document_get_file (document)))
    {
      g_autoptr(GFile) dir = g_file_get_parent (dfile);

      if (dir != NULL)
        gtk_file_chooser_set_current_folder_file (GTK_FILE_CHOOSER (native), dir, NULL);
    }

  all_files = gtk_file_filter_new ();
  gtk_file_filter_set_name (all_files, _("All Files"));
  gtk_file_filter_add_pattern (all_files, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (native), g_object_ref (all_files));

  text_files = gtk_file_filter_new ();
  gtk_file_filter_set_name (text_files, _("Text Files"));
  gtk_file_filter_add_mime_type (text_files, "text/plain");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (native), g_object_ref (text_files));
  gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (native), text_files);

  g_signal_connect_object (native,
                           "response",
                           G_CALLBACK (editor_window_actions_open_response_cb),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_native_dialog_show (GTK_NATIVE_DIALOG (native));
}

static void
editor_window_actions_focus_search_cb (GSimpleAction *action,
                                       GVariant      *param,
                                       gpointer       user_data)
{
  EditorWindow *self = user_data;

  g_assert (EDITOR_IS_WINDOW (self));

  _editor_window_focus_search (self);
}

static void
editor_window_actions_move_left_cb (GSimpleAction *action,
                                    GVariant      *param,
                                    gpointer       user_data)
{
  EditorWindow *self = user_data;
  GtkNotebook *notebook;
  EditorPage *page;
  gint page_num;

  g_assert (EDITOR_IS_WINDOW (self));

  if (!(page = editor_window_get_visible_page (self)))
    return;

  if (!(notebook = GTK_NOTEBOOK (gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_NOTEBOOK))))
    return;

  if ((page_num = gtk_notebook_page_num (notebook, GTK_WIDGET (page))) < 0)
    return;

  gtk_notebook_reorder_child (notebook, GTK_WIDGET (page), page_num - 1);
  _editor_page_raise (page);
}

static void
editor_window_actions_move_right_cb (GSimpleAction *action,
                                     GVariant      *param,
                                     gpointer       user_data)
{
  EditorWindow *self = user_data;
  GtkNotebook *notebook;
  EditorPage *page;
  gint page_num;

  g_assert (EDITOR_IS_WINDOW (self));

  if (!(page = editor_window_get_visible_page (self)))
    return;

  if (!(notebook = GTK_NOTEBOOK (gtk_widget_get_ancestor (GTK_WIDGET (page), GTK_TYPE_NOTEBOOK))))
    return;

  if ((page_num = gtk_notebook_page_num (notebook, GTK_WIDGET (page))) < 0)
    return;

  gtk_notebook_reorder_child (notebook, GTK_WIDGET (page), page_num + 1);
  _editor_page_raise (page);
}

static void
editor_window_actions_move_to_new_window_cb (GSimpleAction *action,
                                             GVariant *param,
                                             gpointer user_data)
{
  EditorWindow *self = user_data;
  EditorWindow *window;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if (!(page = editor_window_get_visible_page (self)))
    return;

  window = _editor_session_create_window_no_draft (EDITOR_SESSION_DEFAULT);
  _editor_session_move_page_to_window (EDITOR_SESSION_DEFAULT, page, window);
  _editor_page_raise (page);
  gtk_window_present (GTK_WINDOW (window));
}

static void
editor_window_actions_begin_search_cb (GSimpleAction *action,
                                       GVariant      *param,
                                       gpointer       user_data)
{
  EditorWindow *self = user_data;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if ((page = editor_window_get_visible_page (self)))
    _editor_page_begin_search (page);
}

static void
editor_window_actions_begin_replace_cb (GSimpleAction *action,
                                        GVariant      *param,
                                        gpointer       user_data)
{
  EditorWindow *self = user_data;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if ((page = editor_window_get_visible_page (self)))
    _editor_page_begin_replace (page);
}

void
_editor_window_class_actions_init (EditorWindowClass *klass)
{
}

void
_editor_window_actions_init (EditorWindow *self)
{
  static const gchar *setting_keys[] = {
    "dark-mode",
    "discover-settings",
    "indent-style",
    "show-line-numbers",
    "show-right-margin",
    "tab-width",
    "wrap-text",
  };

  static const GActionEntry win_actions[] = {
    { "close-current-page", editor_window_actions_close_page_cb },
    { "open", editor_window_actions_open_cb },
    { "focus-search", editor_window_actions_focus_search_cb },
  };

  static const GActionEntry session_actions[] = {
    { "new-draft", editor_window_actions_new_draft_cb },
  };

  static const GActionEntry page_actions[] = {
    { "save", editor_window_actions_save_cb },
    { "save-as", editor_window_actions_save_as_cb },
    { "change-language", editor_window_actions_change_language_cb },
    { "discard-changes", editor_window_actions_discard_changes_cb },
    { "print", editor_window_actions_print_cb },
    { "change", editor_window_actions_change_page_cb, "i" },
    { "copy-all", editor_window_actions_copy_all_cb },
    { "move-left", editor_window_actions_move_left_cb },
    { "move-right", editor_window_actions_move_right_cb },
    { "move-to-new-window", editor_window_actions_move_to_new_window_cb },
    { "begin-search", editor_window_actions_begin_search_cb },
    { "begin-replace", editor_window_actions_begin_replace_cb },
  };

  g_autoptr(GSimpleActionGroup) group = g_simple_action_group_new ();
  g_autoptr(GSimpleActionGroup) session = g_simple_action_group_new ();
  g_autoptr(GSimpleActionGroup) page = g_simple_action_group_new ();
  g_autoptr(GSettings) settings = NULL;

  g_assert (EDITOR_IS_WINDOW (self));

  /* Setup settings actions */
  settings = g_settings_new ("org.gnome.TextEditor");
  for (guint i = 0; i < G_N_ELEMENTS (setting_keys); i++)
    {
      const gchar *key = setting_keys[i];
      g_autoptr(GAction) action = g_settings_create_action (settings, key);
      g_action_map_add_action (G_ACTION_MAP (group), action);
    }
  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "settings",
                                  G_ACTION_GROUP (group));

  /* Add window actions to window (which is an action map) */
  g_action_map_add_action_entries (G_ACTION_MAP (self),
                                   win_actions,
                                   G_N_ELEMENTS (win_actions),
                                   self);

  /* Now add session actions */
  g_action_map_add_action_entries (G_ACTION_MAP (session),
                                   session_actions,
                                   G_N_ELEMENTS (session_actions),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "session",
                                  G_ACTION_GROUP (session));

  /* Now add our page proxy actions */
  g_action_map_add_action_entries (G_ACTION_MAP (page),
                                   page_actions,
                                   G_N_ELEMENTS (page_actions),
                                   self);
  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "page",
                                  G_ACTION_GROUP (page));

  _editor_window_actions_update (self, NULL);
}

void
_editor_window_actions_update (EditorWindow *self,
                               EditorPage   *page)
{
  GActionGroup *group;
  gboolean has_page = FALSE;
  gboolean can_save = FALSE;
  gboolean modified = FALSE;
  gboolean draft = FALSE;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (!page || EDITOR_IS_PAGE (page));

  if (page != NULL)
    {
      has_page = TRUE;
      can_save = editor_page_get_can_save (page);
      modified = editor_page_get_is_modified (page);
      draft = editor_page_is_draft (page);
    }

  group = gtk_widget_get_action_group (GTK_WIDGET (self), "page");

#define ACTION_SET_ENABLED(name, enabled) \
  G_STMT_START { \
    GAction *action = g_action_map_lookup_action (G_ACTION_MAP (group), name); \
    g_simple_action_set_enabled (G_SIMPLE_ACTION (action), enabled); \
  } G_STMT_END

  ACTION_SET_ENABLED ("change-language", has_page);
  ACTION_SET_ENABLED ("discard-changes", modified && !draft);
  ACTION_SET_ENABLED ("print", has_page);
  ACTION_SET_ENABLED ("save", can_save);
  ACTION_SET_ENABLED ("save-as", has_page);
  ACTION_SET_ENABLED ("copy-all", has_page);
  ACTION_SET_ENABLED ("begin-replace", has_page);
  ACTION_SET_ENABLED ("begin-search", has_page);

#undef ACTION_SET_ENABLED
}
