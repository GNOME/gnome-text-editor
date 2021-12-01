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
#include "editor-preferences-dialog-private.h"
#include "editor-save-changes-dialog-private.h"
#include "editor-session-private.h"
#include "editor-window-private.h"

static void
editor_window_actions_new_draft_cb (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorSession *session;

  g_assert (EDITOR_IS_WINDOW (self));

  session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);
  editor_session_add_draft (session, self);
}

static void
editor_window_actions_close_page_cb (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if (editor_window_get_visible_page (self) == NULL)
    {
      gtk_window_close (GTK_WINDOW (self));
      return;
    }

  if ((page = editor_window_get_visible_page (self)) &&
      _editor_window_request_close_page (self, page))
    {
      EditorSession *session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);

      editor_session_remove_page (session, page);
    }
}

static void
editor_window_actions_save_cb (GtkWidget  *widget,
                               const char *action_name,
                               GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  page = editor_window_get_visible_page (self);

  if (page != NULL)
    _editor_page_save (page);
}

static void
editor_window_actions_confirm_save_response_cb (GtkMessageDialog *dialog,
                                                int               response,
                                                EditorPage       *page)
{
  g_assert (GTK_IS_MESSAGE_DIALOG (dialog));
  g_assert (EDITOR_IS_PAGE (page));

  if (response == GTK_RESPONSE_YES)
    _editor_page_save (page);

  gtk_window_destroy (GTK_WINDOW (dialog));
}

static void
editor_window_actions_confirm_save_cb (GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  g_autofree gchar *title = NULL;
  EditorPage *page;
  GtkWidget *dialog;
  GtkWidget *save;

  g_assert (EDITOR_IS_WINDOW (self));

  if (!(page = editor_window_get_visible_page (self)))
    return;

  title = editor_page_dup_title (page);
  dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   /* translators: %s is replaced with the document title */
                                   _("Save Changes to “%s”?"),
                                   title);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("Saving changes will replace the previously saved version."));
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
  save = gtk_dialog_add_button (GTK_DIALOG (dialog), _("Save"), GTK_RESPONSE_YES);
  gtk_widget_add_css_class (save, "destructive-action");

  g_signal_connect_object (dialog,
                           "response",
                           G_CALLBACK (editor_window_actions_confirm_save_response_cb),
                           page,
                           0);

  gtk_window_present (GTK_WINDOW (dialog));
}

static void
editor_window_actions_save_as_cb (GtkWidget  *widget,
                                  const char *action_name,
                                  GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  page = editor_window_get_visible_page (self);

  if (page != NULL)
    _editor_page_save_as (page, NULL);
}

static void
editor_window_actions_change_language_cb (GtkWidget  *widget,
                                          const char *action_name,
                                          GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
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
editor_window_actions_discard_changes_cb (GtkWidget  *widget,
                                          const char *action_name,
                                          GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if ((page = editor_window_get_visible_page (self)))
    _editor_page_discard_changes (page);
}

static void
editor_window_actions_confirm_discard_response_cb (GtkMessageDialog *dialog,
                                                   int               response,
                                                   EditorPage       *page)
{
  g_assert (GTK_IS_MESSAGE_DIALOG (dialog));
  g_assert (EDITOR_IS_PAGE (page));

  if (response == GTK_RESPONSE_YES)
    _editor_page_discard_changes (page);

  gtk_window_destroy (GTK_WINDOW (dialog));
}

static void
editor_window_actions_confirm_discard_changes_cb (GtkWidget  *widget,
                                                  const char *action_name,
                                                  GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  g_autofree gchar *title = NULL;
  EditorPage *page;
  GtkWidget *dialog;
  GtkWidget *discard;

  g_assert (EDITOR_IS_WINDOW (self));

  if (!(page = editor_window_get_visible_page (self)))
    return;

  title = editor_page_dup_title (page);
  dialog = gtk_message_dialog_new (GTK_WINDOW (self),
                                   GTK_DIALOG_MODAL,
                                   GTK_MESSAGE_QUESTION,
                                   GTK_BUTTONS_NONE,
                                   /* translators: %s is replaced with the document title */
                                   _("Discard Changes to “%s”?"),
                                   title);
  gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                            _("Unsaved changes will be permanently lost."));
  gtk_dialog_add_button (GTK_DIALOG (dialog), _("Cancel"), GTK_RESPONSE_CANCEL);
  discard = gtk_dialog_add_button (GTK_DIALOG (dialog), _("Discard"), GTK_RESPONSE_YES);
  gtk_widget_add_css_class (discard, "destructive-action");

  g_signal_connect_object (dialog,
                           "response",
                           G_CALLBACK (editor_window_actions_confirm_discard_response_cb),
                           page,
                           0);

  gtk_window_present (GTK_WINDOW (dialog));
}

static void
editor_window_actions_print_cb (GtkWidget  *widget,
                                const char *action_name,
                                GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if ((page = editor_window_get_visible_page (self)))
    _editor_page_print (page);
}

static void
editor_window_actions_change_page_cb (GtkWidget  *widget,
                                      const char *action_name,
                                      GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorPage *page;
  gint page_num;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_INT32));

  page_num = g_variant_get_int32 (param) - 1;
  if (page_num < 0 || page_num >= editor_window_get_n_pages (self))
    return;

  if ((page = editor_window_get_nth_page (self, page_num)))
    _editor_page_raise (page);
}

static void
editor_window_actions_copy_all_cb (GtkWidget  *widget,
                                   const char *action_name,
                                   GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
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
      g_autoptr(GListModel) files = gtk_file_chooser_get_files (GTK_FILE_CHOOSER (native));
      guint i = 0;
      GFile *file = NULL;
      const GtkSourceEncoding *encoding = _editor_file_chooser_get_encoding (GTK_FILE_CHOOSER (native));

      g_assert (g_list_model_get_item_type (files) == G_TYPE_FILE);

      while ((file = G_FILE (g_list_model_get_object (files, i++))))
        {
          editor_session_open (EDITOR_SESSION_DEFAULT, self, file, encoding);
          g_object_unref (file);
        }
    }

  gtk_native_dialog_destroy (GTK_NATIVE_DIALOG (native));
}

static void
editor_window_actions_open_cb (GtkWidget  *widget,
                               const char *action_name,
                               GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  g_autoptr(GtkFileFilter) all_files = NULL;
  g_autoptr(GtkFileFilter) text_files = NULL;
  GtkFileChooserNative *native;
  EditorDocument *document;
  EditorPage *page;
  GFile *dfile;

  g_assert (EDITOR_IS_WINDOW (self));

  gtk_menu_button_popdown (self->open_menu_button);

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
        gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (native), dir, NULL);
    }
  else
    {
      g_autoptr(GSettings) settings = g_settings_new ("org.gnome.TextEditor");
      g_autofree char *uri = g_settings_get_string (settings, "last-save-directory");

      if (uri && uri[0])
        {
          g_autoptr(GFile) dir = g_file_new_for_uri (uri);
          gtk_file_chooser_set_current_folder (GTK_FILE_CHOOSER (native), dir, NULL);
        }
    }

  all_files = gtk_file_filter_new ();
  gtk_file_filter_set_name (all_files, _("All Files"));
  gtk_file_filter_add_pattern (all_files, "*");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (native), g_object_ref (all_files));

  text_files = gtk_file_filter_new ();
  gtk_file_filter_set_name (text_files, _("Text Files"));
  gtk_file_filter_add_mime_type (text_files, "text/plain");
  gtk_file_chooser_add_filter (GTK_FILE_CHOOSER (native), g_object_ref (text_files));

#ifdef __APPLE__
  /* Apple content-type detect is pretty bad */
  gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (native), all_files);
#else
  gtk_file_chooser_set_filter (GTK_FILE_CHOOSER (native), text_files);
#endif

  _editor_file_chooser_add_encodings (GTK_FILE_CHOOSER (native));

  gtk_file_chooser_set_select_multiple (GTK_FILE_CHOOSER (native), TRUE);

  g_signal_connect_object (native,
                           "response",
                           G_CALLBACK (editor_window_actions_open_response_cb),
                           self,
                           G_CONNECT_SWAPPED);

  gtk_native_dialog_show (GTK_NATIVE_DIALOG (native));
}

static void
editor_window_actions_show_primary_menu_cb (GtkWidget  *widget,
                                            const char *action_name,
                                            GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;

  g_assert (EDITOR_IS_WINDOW (self));

  gtk_menu_button_popup (self->primary_menu);
}

static void
editor_window_actions_focus_search_cb (GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;

  g_assert (EDITOR_IS_WINDOW (self));

  _editor_window_focus_search (self);
}

static void
editor_window_actions_move_left_cb (GtkWidget  *widget,
                                    const char *action_name,
                                    GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorPage *page;
  AdwTabPage *tab_page;

  g_assert (EDITOR_IS_WINDOW (self));

  if (!(page = editor_window_get_visible_page (self)))
    return;

  tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (page));
  adw_tab_view_reorder_backward (self->tab_view, tab_page);
  _editor_page_raise (page);
}

static void
editor_window_actions_move_right_cb (GtkWidget  *widget,
                                     const char *action_name,
                                     GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorPage *page;
  AdwTabPage *tab_page;

  g_assert (EDITOR_IS_WINDOW (self));

  if (!(page = editor_window_get_visible_page (self)))
    return;

  tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (page));
  adw_tab_view_reorder_forward (self->tab_view, tab_page);
  _editor_page_raise (page);
}

static void
editor_window_actions_move_to_new_window_cb (GtkWidget  *widget,
                                             const char *action_name,
                                             GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
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
editor_window_actions_begin_search_cb (GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if ((page = editor_window_get_visible_page (self)))
    _editor_page_begin_search (page);
}

static void
editor_window_actions_begin_replace_cb (GtkWidget  *widget,
                                        const char *action_name,
                                        GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if ((page = editor_window_get_visible_page (self)))
    _editor_page_begin_replace (page);
}

static void
editor_window_actions_focus_neighbor_cb (GtkWidget  *widget,
                                         const char *action_name,
                                         GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;

  g_assert (EDITOR_IS_WINDOW (self));

  if (g_variant_get_int32 (param) == -1)
    adw_tab_view_select_previous_page (self->tab_view);
  else
    adw_tab_view_select_next_page (self->tab_view);
}

static void
editor_window_actions_show_preferences_cb (GtkWidget  *widget,
                                           const char *action_name,
                                           GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  GtkWidget *dialog;

  g_assert (EDITOR_IS_WINDOW (self));

  dialog = editor_preferences_dialog_new (self);
  gtk_window_present (GTK_WINDOW (dialog));
}

static void
editor_window_actions_close_other_pages_cb (GtkWidget  *widget,
                                            const char *action_name,
                                            GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorPage *current_page;
  GList *pages;

  g_assert (EDITOR_IS_WINDOW (self));

  current_page = editor_window_get_visible_page (self);
  pages = _editor_window_get_pages (self);
  pages = g_list_remove (pages, current_page);
  _editor_window_request_close_pages (self, pages, TRUE);
  g_list_free (pages);
}

static void
editor_window_actions_page_zoom_in_cb (GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;

  g_assert (EDITOR_IS_WINDOW (self));

  _editor_page_zoom_in (editor_window_get_visible_page (self));
}

static void
editor_window_actions_page_zoom_out_cb (GtkWidget  *widget,
                                        const char *action_name,
                                        GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;

  g_assert (EDITOR_IS_WINDOW (self));

  _editor_page_zoom_out (editor_window_get_visible_page (self));
}

void
_editor_window_class_actions_init (EditorWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  gtk_widget_class_install_action (widget_class,
                                   "session.new-draft",
                                   NULL,
                                   editor_window_actions_new_draft_cb);
  gtk_widget_class_install_action (widget_class,
                                   "win.close-current-page",
                                   NULL,
                                   editor_window_actions_close_page_cb);
  gtk_widget_class_install_action (widget_class,
                                   "win.close-page-or-window",
                                   NULL,
                                   editor_window_actions_close_page_cb);
  gtk_widget_class_install_action (widget_class,
                                   "win.close-other-pages",
                                   NULL,
                                   editor_window_actions_close_other_pages_cb);
  gtk_widget_class_install_action (widget_class,
                                   "win.open",
                                   NULL,
                                   editor_window_actions_open_cb);
  gtk_widget_class_install_action (widget_class,
                                   "win.show-primary-menu",
                                   NULL,
                                   editor_window_actions_show_primary_menu_cb);
  gtk_widget_class_install_action (widget_class,
                                   "win.focus-search",
                                   NULL,
                                   editor_window_actions_focus_search_cb);
  gtk_widget_class_install_action (widget_class,
                                   "win.focus-neighbor",
                                   "i",
                                   editor_window_actions_focus_neighbor_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.save",
                                   NULL,
                                   editor_window_actions_save_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.confirm-save",
                                   NULL,
                                   editor_window_actions_confirm_save_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.save-as",
                                   NULL,
                                   editor_window_actions_save_as_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.change-language",
                                   NULL,
                                   editor_window_actions_change_language_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.discard-changes",
                                   NULL,
                                   editor_window_actions_discard_changes_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.confirm-discard-changes",
                                   NULL,
                                   editor_window_actions_confirm_discard_changes_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.print",
                                   NULL,
                                   editor_window_actions_print_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.change",
                                   "i",
                                   editor_window_actions_change_page_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.copy-all",
                                   NULL,
                                   editor_window_actions_copy_all_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.move-left",
                                   NULL,
                                   editor_window_actions_move_left_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.move-right",
                                   NULL,
                                   editor_window_actions_move_right_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.move-to-new-window",
                                   NULL,
                                   editor_window_actions_move_to_new_window_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.begin-search",
                                   NULL,
                                   editor_window_actions_begin_search_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.begin-replace",
                                   NULL,
                                   editor_window_actions_begin_replace_cb);
  gtk_widget_class_install_action (widget_class,
                                   "win.show-preferences",
                                   NULL,
                                   editor_window_actions_show_preferences_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.zoom-in",
                                   NULL,
                                   editor_window_actions_page_zoom_in_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.zoom-out",
                                   NULL,
                                   editor_window_actions_page_zoom_out_cb);
}

void
_editor_window_actions_init (EditorWindow *self)
{
  g_autoptr(GSimpleActionGroup) group = NULL;
  g_autoptr(GSettings) settings = NULL;
  static const gchar *setting_keys[] = {
    "auto-indent",
    "discover-settings",
    "indent-style",
    "show-line-numbers",
    "show-right-margin",
    "spellcheck",
    "style-variant",
    "tab-width",
    "wrap-text",
  };

  g_assert (EDITOR_IS_WINDOW (self));

  settings = g_settings_new ("org.gnome.TextEditor");
  group = g_simple_action_group_new ();

  for (guint i = 0; i < G_N_ELEMENTS (setting_keys); i++)
    {
      const gchar *key = setting_keys[i];
      g_autoptr(GAction) action = g_settings_create_action (settings, key);
      g_action_map_add_action (G_ACTION_MAP (group), action);
    }

  gtk_widget_insert_action_group (GTK_WIDGET (self),
                                  "settings",
                                  G_ACTION_GROUP (group));

  _editor_window_actions_update (self, NULL);
}

void
_editor_window_actions_update (EditorWindow *self,
                               EditorPage   *page)
{
  gboolean externally_modified = FALSE;
  gboolean has_page = FALSE;
  gboolean can_save = FALSE;
  gboolean modified = FALSE;
  gboolean draft = FALSE;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (!page || EDITOR_IS_PAGE (page));

  if (page != NULL)
    {
      EditorDocument *document = editor_page_get_document (page);

      has_page = TRUE;
      can_save = editor_page_get_can_save (page);
      modified = editor_page_get_is_modified (page);
      draft = editor_page_is_draft (page);
      externally_modified = editor_document_get_externally_modified (document);
    }

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.close-current-page", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.close-other-pages", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.change-language", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.discard-changes", externally_modified || (modified && !draft));
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.print", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.save", can_save);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.save-as", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.copy-all", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.begin-replace", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.begin-search", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.zoom-in", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.zoom-out", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.focus-neighbor", has_page);
}
