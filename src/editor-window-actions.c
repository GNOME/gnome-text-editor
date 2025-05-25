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
#include "editor-encoding-dialog.h"
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
editor_window_actions_confirm_save_cb (GtkWidget  *widget,
                                       const char *action_name,
                                       GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  g_autofree gchar *title = NULL;
  EditorPage *page;
  AdwDialog *dialog;

  g_assert (EDITOR_IS_WINDOW (self));

  if (!(page = editor_window_get_visible_page (self)))
    return;

  title = editor_page_dup_title (page);
  dialog = adw_alert_dialog_new (NULL,
                                 _("Saving changes will replace the previously saved version."));
  adw_alert_dialog_format_heading (ADW_ALERT_DIALOG (dialog),
                                   /* translators: %s is replaced with the document title */
                                   _("Save Changes to “%s”?"),
                                   title);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                  "cancel", _("_Cancel"),
                                  "save", _("_Save"),
                                  NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                            "save", ADW_RESPONSE_DESTRUCTIVE);

  g_signal_connect_object (dialog,
                           "response::save",
                           G_CALLBACK (_editor_page_save),
                           page,
                           G_CONNECT_SWAPPED);

  adw_dialog_present (dialog, widget);
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
editor_window_actions_notify_language_cb (EditorDocument       *document,
                                          GParamSpec           *pspec,
                                          EditorLanguageDialog *dialog)
{
  GtkSourceLanguage *language;
  const char *language_id = NULL;

  g_assert (EDITOR_IS_DOCUMENT (document));
  g_assert (EDITOR_IS_LANGUAGE_DIALOG (dialog));

  if ((language = editor_language_dialog_get_language (dialog)))
    language_id = gtk_source_language_get_id (language);

  _editor_document_persist_syntax_language (document, language_id);
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

  dialog = editor_language_dialog_new ();

  g_object_bind_property (document, "language", dialog, "language",
                          G_BINDING_SYNC_CREATE | G_BINDING_BIDIRECTIONAL);

  /* We only want to persist the language to underlying storage if the
   * user changes the value using the language dialog. Otherwise, we can't
   * update to better content-type discovery in updates of shared-mime-info
   * or GtkSourceView as they'd be pinned to the first result we ever get.
   */
  g_signal_connect_object (dialog,
                           "notify::language",
                           G_CALLBACK (editor_window_actions_notify_language_cb),
                           document,
                           G_CONNECT_SWAPPED);

  adw_dialog_present (ADW_DIALOG (dialog), widget);
}

static void
editor_window_actions_confirm_discard_changes (GtkWidget *widget)
{
  EditorWindow *self = (EditorWindow *)widget;
  g_autofree gchar *title = NULL;
  EditorPage *page;
  AdwDialog *dialog;

  g_assert (EDITOR_IS_WINDOW (self));

  if (!(page = editor_window_get_visible_page (self)))
    return;

  title = editor_page_dup_title (page);
  dialog = adw_alert_dialog_new (NULL, _("Unsaved changes will be permanently lost."));
  adw_alert_dialog_format_heading (ADW_ALERT_DIALOG (dialog),
                                     /* translators: %s is replaced with the document title */
                                     _("Discard Changes to “%s”?"),
                                     title);
  adw_alert_dialog_add_responses (ADW_ALERT_DIALOG (dialog),
                                    "cancel", _("_Cancel"),
                                    "discard", _("_Discard"),
                                    NULL);
  adw_alert_dialog_set_response_appearance (ADW_ALERT_DIALOG (dialog),
                                              "discard", ADW_RESPONSE_DESTRUCTIVE);

  g_signal_connect_object (dialog,
                           "response::discard",
                           G_CALLBACK (_editor_page_discard_changes),
                           page,
                           G_CONNECT_SWAPPED);

  adw_dialog_present (dialog, widget);
}

static void
editor_window_actions_discard_changes_cb (GtkWidget  *widget,
                                          const char *action_name,
                                          GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (g_variant_is_of_type (param, G_VARIANT_TYPE_BOOLEAN));

  if (!g_variant_get_boolean (param))
    editor_window_actions_confirm_discard_changes (widget);
  else
    _editor_page_discard_changes (editor_window_get_visible_page (self));
}

static void
editor_window_actions_infobar_discard_changes_cb (GtkWidget  *widget,
                                                  const char *action_name,
                                                  GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;

  g_assert (EDITOR_IS_WINDOW (self));

  editor_window_actions_confirm_discard_changes (widget);
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
editor_window_actions_open_text_file_cb (GObject      *object,
                                         GAsyncResult *result,
                                         gpointer      user_data)
{
  GtkFileDialog *dialog = (GtkFileDialog *)object;
  g_autoptr(EditorWindow) self = user_data;
  g_autoptr(GError) error = NULL;
  g_autoptr(GListModel) files = NULL;
  const char *encoding = NULL;

  g_assert (GTK_IS_FILE_DIALOG (dialog));
  g_assert (G_IS_ASYNC_RESULT (result));
  g_assert (EDITOR_IS_WINDOW (self));

  if ((files = gtk_file_dialog_open_multiple_text_files_finish (dialog, result, &encoding, &error)))
    {
      const GtkSourceEncoding *translated = NULL;
      guint n_items = g_list_model_get_n_items (files);

      if (encoding != NULL && g_strcmp0 (encoding, "automatic") != 0)
        translated = gtk_source_encoding_get_from_charset (encoding);

      for (guint i = 0; i < n_items; i++)
        {
          g_autoptr(GFile) file = g_list_model_get_item (files, i);
          editor_session_open (EDITOR_SESSION_DEFAULT, self, file, translated);
        }
    }
}

static void
editor_window_actions_open_cb (GtkWidget  *widget,
                               const char *action_name,
                               GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  g_autoptr(GtkFileFilter) all_files = NULL;
  g_autoptr(GtkFileFilter) text_files = NULL;
  g_autoptr(GtkFileDialog) dialog = NULL;
  g_autoptr(GListStore) filters = NULL;
  EditorDocument *document;
  EditorPage *page;
  GFile *dfile;

  g_assert (EDITOR_IS_WINDOW (self));

  gtk_menu_button_popdown (self->open_menu_button);

  dialog = gtk_file_dialog_new ();
  filters = g_list_store_new (GTK_TYPE_FILE_FILTER);

  if ((page = editor_window_get_visible_page (self)) &&
      (document = editor_page_get_document (page)) &&
      (dfile = editor_document_get_file (document)))
    {
      g_autoptr(GFile) dir = g_file_get_parent (dfile);

      if (dir != NULL)
        gtk_file_dialog_set_initial_folder (dialog, dir);
    }
  else
    {
      g_autoptr(GSettings) settings = g_settings_new ("org.gnome.TextEditor");
      g_autofree char *uri = g_settings_get_string (settings, "last-save-directory");

      if (uri && uri[0])
        {
          g_autoptr(GFile) dir = g_file_new_for_uri (uri);
          gtk_file_dialog_set_initial_folder (dialog, dir);
        }
    }

  all_files = gtk_file_filter_new ();
  gtk_file_filter_set_name (all_files, _("All Files"));
  gtk_file_filter_add_pattern (all_files, "*");
  g_list_store_append (filters, all_files);

  text_files = gtk_file_filter_new ();
  gtk_file_filter_set_name (text_files, _("Text Files"));
  gtk_file_filter_add_mime_type (text_files, "text/plain");
  gtk_file_filter_add_mime_type (text_files, "application/x-zerosize");
  g_list_store_append (filters, text_files);

  gtk_file_dialog_set_filters (dialog, G_LIST_MODEL (filters));

#ifdef __APPLE__
  /* Apple content-type detect is pretty bad */
  gtk_file_dialog_set_default_filter (dialog, all_files);
#else
  gtk_file_dialog_set_default_filter (dialog, text_files);
#endif

  gtk_file_dialog_open_multiple_text_files (dialog,
                                            GTK_WINDOW (self),
                                            NULL,
                                            editor_window_actions_open_text_file_cb,
                                            g_object_ref (self));
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
  AdwDialog *dialog;

  g_assert (EDITOR_IS_WINDOW (self));

  dialog = editor_preferences_dialog_new (self);
  adw_dialog_present (dialog, widget);
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
editor_window_actions_change_encoding_cb (GtkWidget  *widget,
                                          const char *action_name,
                                          GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;
  EditorDocument *document;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if ((page = editor_window_get_visible_page (self)) &&
      (document = editor_page_get_document (page)))
    {
      AdwDialog *dialog = editor_encoding_dialog_new (document);

      adw_dialog_present (dialog, GTK_WIDGET (self));
    }
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

static void
editor_window_actions_page_zoom_one_cb (GtkWidget  *widget,
                                        const char *action_name,
                                        GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;

  g_assert (EDITOR_IS_WINDOW (self));

  _editor_page_zoom_one (editor_window_get_visible_page (self));
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
                                   "b",
                                   editor_window_actions_discard_changes_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.infobar-discard-changes",
                                   NULL,
                                   editor_window_actions_infobar_discard_changes_cb);
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
                                   "page.change-encoding",
                                   NULL,
                                   editor_window_actions_change_encoding_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.zoom-in",
                                   NULL,
                                   editor_window_actions_page_zoom_in_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.zoom-out",
                                   NULL,
                                   editor_window_actions_page_zoom_out_cb);
  gtk_widget_class_install_action (widget_class,
                                   "page.zoom-one",
                                   NULL,
                                   editor_window_actions_page_zoom_one_cb);
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
  int font_scale = 0;

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
      g_object_get (page->view, "font-scale", &font_scale, NULL);
    }

  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.close-current-page", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.close-other-pages", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.change-language", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.discard-changes", externally_modified || (modified && !draft));
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.print", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.change-encoding", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.save", can_save);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.save-as", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.copy-all", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.begin-replace", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.begin-search", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.zoom-in", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.zoom-out", has_page);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.focus-neighbor", has_page);

  /* HACK: Work around GNOME/gtk#7296 by forcing the action on then possibly
   *       immediately back off.
   */
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.zoom-one", TRUE);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "page.zoom-one", has_page && font_scale != 0);
}
