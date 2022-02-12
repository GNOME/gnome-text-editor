/* editor-window.c
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

#define G_LOG_DOMAIN "editor-window"

#include "config.h"

#include <glib/gi18n.h>

#include "editor-application.h"
#include "editor-document.h"
#include "editor-open-popover-private.h"
#include "editor-save-changes-dialog-private.h"
#include "editor-session-private.h"
#include "editor-theme-selector-private.h"
#include "editor-utils-private.h"
#include "editor-window-private.h"

typedef struct
{
  char *draft_id;
  GFile *file;
  const GtkSourceEncoding *encoding;
} ClosedItem;

G_DEFINE_TYPE (EditorWindow, editor_window, ADW_TYPE_APPLICATION_WINDOW)

enum {
  PROP_0,
  PROP_VISIBLE_PAGE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static void
closed_item_clear (gpointer data)
{
  ClosedItem *ci = data;

  g_clear_pointer (&ci->draft_id, g_free);
  g_clear_object (&ci->file);
}

static void
add_closed_document (EditorWindow   *self,
                     EditorDocument *document)
{
  ClosedItem ci = {0};
  GFile *file;
  const char *draft_id;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  file = editor_document_get_file (document);
  draft_id = _editor_document_get_draft_id (document);

  g_assert (file || draft_id);

  for (guint i = 0; i < self->closed_items->len; i++)
    {
      const ClosedItem *ele = &g_array_index (self->closed_items, ClosedItem, i);

      if ((file && ele->file == file) || (draft_id && ele->draft_id == draft_id))
        return;
    }

  if (file)
    ci.file = g_file_dup (file);
  else
    ci.draft_id = g_strdup (draft_id);

  ci.encoding = _editor_document_get_encoding (document);

  g_array_append_val (self->closed_items, ci);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.undo-close-page", TRUE);
}

static void
remove_page (EditorWindow *self,
             EditorPage   *page)
{
  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (EDITOR_IS_PAGE (page));

  /* Track page close for reopening */
  if (!editor_page_get_can_discard (page))
    {
      EditorDocument *document = editor_page_get_document (page);
      add_closed_document (self, document);
    }

  editor_session_remove_page (EDITOR_SESSION_DEFAULT, page);
}

static void
restore_closed_document (EditorWindow *self)
{
  const ClosedItem *ci;

  g_assert (EDITOR_IS_WINDOW (self));

  if (self->closed_items->len == 0)
    return;

  ci = &g_array_index (self->closed_items, ClosedItem, self->closed_items->len - 1);

  if (ci->file)
    editor_session_open (EDITOR_SESSION_DEFAULT, self, ci->file, ci->encoding);
  else
    _editor_session_open_draft (EDITOR_SESSION_DEFAULT, self, ci->draft_id);

  g_array_set_size (self->closed_items, self->closed_items->len - 1);
  gtk_widget_action_set_enabled (GTK_WIDGET (self),
                                 "win.undo-close-page",
                                 self->closed_items->len > 0);
}

static void
on_undo_close_page_cb (GtkWidget  *widget,
                       const char *action_name,
                       GVariant   *param)
{
  EditorWindow *self = (EditorWindow *)widget;

  g_assert (EDITOR_IS_WINDOW (self));

  restore_closed_document (self);
}

static void
update_keybindings_cb (EditorWindow *self,
                       const char   *key,
                       GSettings    *settings)
{
  g_autofree char *keybindings = NULL;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (G_IS_SETTINGS (settings));

  /* Show the statusbar if we have vim keybindings enabled */
  keybindings = g_settings_get_string (settings, "keybindings");
  gtk_widget_set_visible (GTK_WIDGET (self->statusbar), g_strcmp0 (keybindings, "vim") == 0);
}

static void
update_subtitle_visibility_cb (EditorWindow *self)
{
  gboolean visible = FALSE;

  g_assert (EDITOR_IS_WINDOW (self));

  if (self->visible_page != NULL)
    {
      EditorPage *page = self->visible_page;
      EditorDocument *buffer = editor_page_get_document (page);

      if (gtk_text_buffer_get_modified (GTK_TEXT_BUFFER (buffer)) ||
          !editor_page_get_can_discard (page))
        visible = TRUE;
    }

  gtk_widget_set_visible (GTK_WIDGET (self->subtitle), visible);

  if (visible)
    gtk_widget_set_visible (GTK_WIDGET (self->position_box),
                            g_settings_get_boolean (self->settings, "show-line-numbers"));
  else
    gtk_widget_hide (GTK_WIDGET (self->position_box));
}

static void
update_language_name_cb (EditorWindow *self)
{
  const char *language_name = NULL;
  const char *label = _("Document Type");
  g_autofree char *freeme = NULL;
  EditorPage *page;

  g_assert (EDITOR_IS_WINDOW (self));

  if ((page = editor_window_get_visible_page (self)))
    language_name = editor_page_get_language_name (page);

  g_menu_remove (G_MENU (self->doc_type_menu), self->doc_type_index);

   if (language_name)
     label = freeme = g_strdup_printf (_("Document Type: %s"), language_name);

   g_menu_insert (G_MENU (self->doc_type_menu),
                  self->doc_type_index,
                  label,
                  "page.change-language");
}

static void
editor_window_cursor_moved_cb (EditorWindow   *self,
                               EditorDocument *document)
{
  EditorPage *page;
  guint line;
  guint column;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (self->position_label != NULL);
  g_assert (EDITOR_IS_POSITION_LABEL (self->position_label));
  g_assert (EDITOR_IS_DOCUMENT (document));

  if (!(page = editor_window_get_visible_page (self)))
    return;

  if (editor_document_get_busy (document))
    return;

  if (editor_page_get_document (page) != document)
    return;

  editor_page_get_visual_position (page, &line, &column);
  _editor_position_label_set_position (self->position_label, line + 1, column + 1);
}

static void
editor_window_page_bind_cb (EditorWindow      *self,
                            EditorPage        *page,
                            EditorSignalGroup *group)
{
  EditorDocument *document;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (EDITOR_IS_PAGE (page));
  g_assert (EDITOR_IS_SIGNAL_GROUP (group));

  document = editor_page_get_document (page);

  editor_signal_group_set_target (self->document_signals, document);
  editor_window_cursor_moved_cb (self, document);

  _editor_window_actions_update (self, page);

  update_subtitle_visibility_cb (self);
  update_language_name_cb (self);
}

static void
editor_window_page_unbind_cb (EditorWindow      *self,
                              EditorSignalGroup *group)
{
  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (EDITOR_IS_SIGNAL_GROUP (group));

  _editor_window_actions_update (self, NULL);
}

static void
editor_window_update_actions (EditorWindow *self)
{
  g_assert (EDITOR_IS_WINDOW (self));

  _editor_window_actions_update (self, self->visible_page);
}

static void
editor_window_notify_selected_page_cb (EditorWindow *self,
                                       GParamSpec   *pspec,
                                       AdwTabView   *tab_view)
{
  EditorPage *page = NULL;
  AdwTabPage *tab_page;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_VIEW (tab_view));

  if ((tab_page = adw_tab_view_get_selected_page (self->tab_view)))
    page = EDITOR_PAGE (adw_tab_page_get_child (tab_page));

  if (self->visible_page == page)
    return;

  g_assert (!page || EDITOR_IS_PAGE (page));

  gtk_label_set_label (self->title, _(PACKAGE_NAME));
  gtk_label_set_label (self->subtitle, NULL);
  gtk_label_set_label (GTK_LABEL (self->zoom_label), "100%");
  gtk_widget_set_sensitive (self->zoom_label, page != NULL);
  gtk_widget_set_visible (GTK_WIDGET (self->is_modified), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->subtitle), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->position_box),
                          page && g_settings_get_boolean (self->settings, "show-line-numbers"));
  gtk_widget_set_visible (GTK_WIDGET (self->indicator), FALSE);

  self->visible_page = page;

  editor_binding_group_set_source (self->page_bindings, page);
  editor_signal_group_set_target (self->page_signals, page);
  editor_statusbar_bind_page (self->statusbar, page);

  _editor_window_actions_update (self, page);

  if (page != NULL)
    editor_page_grab_focus (page);

  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_VISIBLE_PAGE]);

  /* FIXME: Sometimes we get "empty" contents for a tab page when
   *        switching with the accelerators. This seems to make that
   *        go away.
   */
  gtk_widget_queue_resize (GTK_WIDGET (self->tab_view));
}

static void
editor_window_items_changed_cb (EditorWindow      *self,
                                guint              position,
                                guint              removed,
                                guint              added,
                                GtkSelectionModel *model)
{
  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (GTK_IS_SELECTION_MODEL (model));

  if (editor_window_get_n_pages (self) > 0)
    gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->pages));
  else
    gtk_stack_set_visible_child (self->stack, GTK_WIDGET (self->empty));
}

static void
editor_window_do_close (EditorWindow *self)
{
  g_assert (EDITOR_IS_WINDOW (self));

  g_signal_handlers_disconnect_by_func (adw_tab_view_get_pages (self->tab_view),
                                        G_CALLBACK (editor_window_items_changed_cb),
                                        self);
  g_signal_handlers_disconnect_by_func (self->tab_view,
                                        G_CALLBACK (editor_window_notify_selected_page_cb),
                                        self);

  _editor_session_remove_window (EDITOR_SESSION_DEFAULT, self);

  self->visible_page = NULL;
}

static void
editor_window_confirm_cb (GObject      *object,
                          GAsyncResult *result,
                          gpointer      user_data)
{
  g_autoptr(EditorWindow) self = user_data;
  g_autoptr(GError) error = NULL;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (G_IS_ASYNC_RESULT (result));

  if (_editor_save_changes_dialog_run_finish (result, &error))
    {
      editor_window_do_close (self);
      gtk_window_destroy (GTK_WINDOW (self));
    }
}

static gboolean
editor_window_close_request (GtkWindow *window)
{
  EditorWindow *self = (EditorWindow *)window;
  g_autoptr(GPtrArray) unsaved = NULL;
  guint n_pages;

  g_assert (EDITOR_IS_WINDOW (self));

  /* If there are documents open that need to be saved, first
   * ask the user what they'd like us to do with them.
   */
  unsaved = g_ptr_array_new_with_free_func (g_object_unref);
  n_pages = adw_tab_view_get_n_pages (self->tab_view);
  for (guint i = 0; i < n_pages; i++)
    {
      EditorPage *page = editor_window_get_nth_page (self, i);

      if (editor_page_get_is_modified (page))
        g_ptr_array_add (unsaved, g_object_ref (page));
    }

  if (unsaved->len > 0)
    {
      _editor_save_changes_dialog_run_async (GTK_WINDOW (self),
                                             unsaved,
                                             NULL,
                                             editor_window_confirm_cb,
                                             g_object_ref (self));
      return TRUE;
    }

  editor_window_do_close (self);

  return GTK_WINDOW_CLASS (editor_window_parent_class)->close_request (window);
}

static gboolean
find_document_type (GMenuModel  *menu,
                    GMenuModel **container,
                    guint       *index)
{
  guint n_items;

  g_assert (G_IS_MENU_MODEL (menu));
  g_assert (container != NULL && *container == NULL);
  g_assert (index != NULL && *index == 0);

  n_items = g_menu_model_get_n_items (menu);

  for (guint i = 0; i < n_items; i++)
    {
      g_autoptr(GMenuLinkIter) iter = NULL;
      g_autofree char *action = NULL;

      if (g_menu_model_get_item_attribute (menu, i, "action", "s", &action))
        {
          if (g_strcmp0 (action, "page.change-language") == 0)
            {
              *container = g_object_ref (menu);
              *index = i;
              return TRUE;
            }
        }

      iter = g_menu_model_iterate_item_links (menu, i);

      while (g_menu_link_iter_next (iter))
        {
          g_autoptr(GMenuModel) link = g_menu_link_iter_get_value (iter);

          if (find_document_type (link, container, index))
            return TRUE;
        }
    }

  return FALSE;
}

static void
editor_window_constructed (GObject *object)
{
  EditorWindow *self = (EditorWindow *)object;
  EditorSession *session;
  GtkPopover *popover;
  GtkWidget *zoom_box;
  GtkWidget *zoom_out;
  GtkWidget *zoom_in;

  g_assert (EDITOR_IS_WINDOW (self));

  G_OBJECT_CLASS (editor_window_parent_class)->constructed (object);

  session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);

  find_document_type (G_MENU_MODEL (self->options_menu_model),
                      &self->doc_type_menu,
                      &self->doc_type_index);
  g_assert (G_IS_MENU_MODEL (self->doc_type_menu));

  /* Set the recents list for the open popover */
  g_object_bind_property (session, "recents",
                          self->open_menu_popover, "model",
                          G_BINDING_SYNC_CREATE);

  /* The primary menu has some custom widgets added to it */
  popover = gtk_menu_button_get_popover (self->primary_menu);
  gtk_popover_menu_add_child (GTK_POPOVER_MENU (popover),
                              _editor_theme_selector_new (),
                              "theme");

  /* Add zoom controls */
  zoom_box = g_object_new (GTK_TYPE_BOX,
                           "spacing", 12,
                           "margin-start", 18,
                           "margin-end", 18,
                           NULL);
  zoom_in = g_object_new (GTK_TYPE_BUTTON,
                          "action-name", "page.zoom-in",
                          "child", g_object_new (GTK_TYPE_IMAGE,
                                                 "icon-name", "list-add-symbolic",
                                                 "pixel-size", 14,
                                                 NULL),
                          NULL);
  gtk_widget_add_css_class (zoom_in, "circular");
  zoom_out = g_object_new (GTK_TYPE_BUTTON,
                           "action-name", "page.zoom-out",
                          "child", g_object_new (GTK_TYPE_IMAGE,
                                                 "icon-name", "list-remove-symbolic",
                                                 "pixel-size", 14,
                                                 NULL),
                           NULL);
  gtk_widget_add_css_class (zoom_out, "circular");
  self->zoom_label = g_object_new (GTK_TYPE_LABEL,
                                   "hexpand", TRUE,
                                   "label", "100%",
                                   NULL);
  editor_binding_group_bind (self->page_bindings, "zoom-label", self->zoom_label, "label", 0);
  gtk_box_append (GTK_BOX (zoom_box), zoom_out);
  gtk_box_append (GTK_BOX (zoom_box), self->zoom_label);
  gtk_box_append (GTK_BOX (zoom_box), zoom_in);
  gtk_popover_menu_add_child (GTK_POPOVER_MENU (popover), zoom_box, "zoom");
}

static gboolean
on_tab_view_close_page_cb (EditorWindow *self,
                           AdwTabPage   *page,
                           AdwTabView   *view)
{
  EditorPage *epage;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_PAGE (page));
  g_assert (ADW_IS_TAB_VIEW (view));

  if (page != adw_tab_view_get_selected_page (view))
    adw_tab_view_set_selected_page (view, page);

  epage = EDITOR_PAGE (adw_tab_page_get_child (page));

  if (epage->moving)
    return TRUE;

  epage->close_requested = TRUE;

  if (_editor_window_request_close_page (self, epage))
    {
      remove_page (self, epage);
      return FALSE;
    }

  return TRUE;
}

static void
editor_window_actions_close_page_confirm_cb (GObject      *object,
                                             GAsyncResult *result,
                                             gpointer      user_data)
{
  g_autoptr(GPtrArray) pages = user_data;
  g_autoptr(GError) error = NULL;
  EditorWindow *self;
  gboolean confirm_close = TRUE;

  g_assert (pages != NULL);
  g_assert (pages->len > 0);

  if (!_editor_save_changes_dialog_run_finish (result, &error))
    {
      g_debug ("Failed to run dialog: %s", error->message);
      confirm_close = FALSE;
    }

  self = EDITOR_WINDOW (gtk_window_get_transient_for (GTK_WINDOW (object)));

  g_signal_handlers_block_by_func (self->tab_view,
                                   G_CALLBACK (on_tab_view_close_page_cb),
                                   self);

  for (guint i = 0; i < pages->len; i++)
    {
      EditorPage *epage = g_ptr_array_index (pages, i);
      AdwTabPage *page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (epage));

      g_assert (EDITOR_IS_PAGE (epage));
      g_assert (ADW_IS_TAB_PAGE (page));

      if (epage->close_requested)
        adw_tab_view_close_page_finish (self->tab_view, page, confirm_close);
      else if (confirm_close)
        adw_tab_view_close_page (self->tab_view, page);

      epage->close_requested = FALSE;
    }

  g_signal_handlers_unblock_by_func (self->tab_view,
                                     G_CALLBACK (on_tab_view_close_page_cb),
                                     self);
}

gboolean
_editor_window_request_close_page (EditorWindow *self,
                                   EditorPage   *page)
{
  GList *list;
  gboolean ret;

  g_return_val_if_fail (EDITOR_IS_WINDOW (self), FALSE);
  g_return_val_if_fail (EDITOR_IS_PAGE (page), FALSE);

  if (page->moving)
    return TRUE;

  list = g_list_append (NULL, page);
  ret = _editor_window_request_close_pages (self, list, FALSE);
  g_list_free (list);

  return ret;
}

gboolean
_editor_window_request_close_pages (EditorWindow *self,
                                    GList        *pages,
                                    gboolean      close_saved)
{
  g_autoptr(GPtrArray) ar = NULL;

  g_return_val_if_fail (EDITOR_IS_WINDOW (self), FALSE);

  if (pages == NULL)
    return TRUE;

  ar = g_ptr_array_new_with_free_func (g_object_unref);

  for (const GList *iter = pages; iter; iter = iter->next)
    {
      EditorPage *page = iter->data;

      if (editor_page_get_is_modified (page))
        g_ptr_array_add (ar, g_object_ref (page));
      else if (close_saved)
        remove_page (self, page);
    }

  if (ar->len == 0)
    return TRUE;

  _editor_save_changes_dialog_run_async (GTK_WINDOW (self),
                                         ar,
                                         NULL,
                                         editor_window_actions_close_page_confirm_cb,
                                         g_ptr_array_ref (ar));

  return FALSE;
}

static void
on_tab_view_setup_menu_cb (EditorWindow *self,
                           AdwTabPage   *page,
                           AdwTabView   *view)
{
  EditorPage *epage;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (!page || ADW_IS_TAB_PAGE (page));
  g_assert (ADW_IS_TAB_VIEW (view));

  if (page == NULL)
    return;

  /* If the tab is not the current page, change to it so that
   * win/page actions apply to the proper page.
   */

  epage = EDITOR_PAGE (adw_tab_page_get_child (page));

  if (epage != self->visible_page)
    editor_window_set_visible_page (self, epage);
}

static AdwTabView *
on_tab_view_create_window_cb (EditorWindow *self,
                              AdwTabView   *tab_view)
{
  EditorWindow *window;
  AdwTabView *ret;

  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (ADW_IS_TAB_VIEW (tab_view));

  window = _editor_session_create_window_no_draft (EDITOR_SESSION_DEFAULT);
  ret = window->tab_view;
  gtk_window_present (GTK_WINDOW (window));

  return ret;
}

static void
notify_focus_widget_cb (EditorWindow *self)
{
  GtkWidget *widget;

  g_assert (EDITOR_IS_WINDOW (self));

  if (!(widget = gtk_root_get_focus (GTK_ROOT (self))))
    {
      EditorPage *page = editor_window_get_visible_page (self);

      if (page != NULL)
        gtk_root_set_focus (GTK_ROOT (self), GTK_WIDGET (page));
    }
}

static gboolean
transform_window_title (GBinding     *binding,
                        const GValue *from,
                        GValue       *to,
                        gpointer      user_data)
{
  g_autoptr(EditorPage) page = EDITOR_PAGE (g_binding_dup_source (binding));
  g_autofree char *title = editor_page_dup_title (page);
  g_autofree char *subtitle = editor_page_dup_subtitle (page);

  g_value_take_string (to,
                       /* translators: the first %s is replaced with the title, the second %s is replaced with the subtitle */
                       g_strdup_printf (_("%s (%s) - Text Editor"),
                                        title, subtitle));

  return TRUE;
}

static void
on_show_help_overlay_cb (GtkWidget  *widget,
                         const char *action_name,
                         GVariant   *param)
{
  g_autoptr(GtkBuilder) builder = NULL;
  GObject *help_overlay;

  g_assert (EDITOR_IS_WINDOW (widget));

  builder = gtk_builder_new_from_resource ("/org/gnome/TextEditor/ui/help-overlay.ui");
  help_overlay = gtk_builder_get_object (builder, "help_overlay");

  if (GTK_IS_SHORTCUTS_WINDOW (help_overlay))
    {
#if DEVELOPMENT_BUILD
      gtk_widget_add_css_class (GTK_WIDGET (help_overlay), "devel");
#endif
      gtk_window_set_transient_for (GTK_WINDOW (help_overlay), GTK_WINDOW (widget));
      gtk_window_present (GTK_WINDOW (help_overlay));
    }
}

static gboolean
indicator_to_boolean (GBinding     *binding,
                      const GValue *from_value,
                      GValue       *to_value,
                      gpointer      user_data)
{
  g_value_set_boolean (to_value, g_value_get_object (from_value) != NULL);
  return TRUE;
}

static void
editor_window_dispose (GObject *object)
{
  EditorWindow *self = (EditorWindow *)object;

  g_assert (EDITOR_IS_WINDOW (self));

  _editor_session_remove_window (EDITOR_SESSION_DEFAULT, self);

  g_clear_object (&self->settings);

  editor_binding_group_set_source (self->page_bindings, NULL);
  editor_signal_group_set_target (self->page_signals, NULL);
  editor_signal_group_set_target (self->document_signals, NULL);

  G_OBJECT_CLASS (editor_window_parent_class)->dispose (object);
}

static void
editor_window_finalize (GObject *object)
{
  EditorWindow *self = (EditorWindow *)object;

  g_assert (EDITOR_IS_WINDOW (self));

  g_clear_object (&self->page_bindings);
  g_clear_object (&self->page_signals);
  g_clear_object (&self->document_signals);
  g_clear_pointer (&self->closed_items, g_array_unref);

  G_OBJECT_CLASS (editor_window_parent_class)->finalize (object);
}

static void
editor_window_get_property (GObject    *object,
                            guint       prop_id,
                            GValue     *value,
                            GParamSpec *pspec)
{
  EditorWindow *self = EDITOR_WINDOW (object);

  switch (prop_id)
    {
    case PROP_VISIBLE_PAGE:
      g_value_set_object (value, editor_window_get_visible_page (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_window_set_property (GObject      *object,
                            guint         prop_id,
                            const GValue *value,
                            GParamSpec   *pspec)
{
  EditorWindow *self = EDITOR_WINDOW (object);

  switch (prop_id)
    {
    case PROP_VISIBLE_PAGE:
      editor_window_set_visible_page (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_window_class_init (EditorWindowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);
  GtkWindowClass *window_class = GTK_WINDOW_CLASS (klass);

  object_class->constructed = editor_window_constructed;
  object_class->dispose = editor_window_dispose;
  object_class->finalize = editor_window_finalize;
  object_class->get_property = editor_window_get_property;
  object_class->set_property = editor_window_set_property;

  window_class->close_request = editor_window_close_request;

  properties [PROP_VISIBLE_PAGE] =
    g_param_spec_object ("visible-page",
                         "Visible Page",
                         "The currently visible page",
                         EDITOR_TYPE_PAGE,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-window.ui");

  gtk_widget_class_bind_template_child (widget_class, EditorWindow, empty);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, indicator);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, is_modified);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, open_menu_button);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, open_menu_popover);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, options_menu);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, options_menu_model);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, pages);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, position_box);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, position_label);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, primary_menu);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, stack);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, subtitle);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, statusbar);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, tab_bar);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, tab_view);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, title);

  gtk_widget_class_bind_template_callback (widget_class, on_tab_view_close_page_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_tab_view_setup_menu_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_tab_view_create_window_cb);

  gtk_widget_class_install_action (widget_class, "win.alternate-help-overlay", NULL, on_show_help_overlay_cb);
  gtk_widget_class_install_action (widget_class, "win.undo-close-page", NULL, on_undo_close_page_cb);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_w, GDK_CONTROL_MASK, "win.close-page-or-window", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_o, GDK_CONTROL_MASK, "win.open", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_k, GDK_CONTROL_MASK, "win.focus-search", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_t, GDK_CONTROL_MASK, "session.new-draft", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_n, GDK_CONTROL_MASK, "app.new-window", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_s, GDK_CONTROL_MASK, "page.save", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_s, GDK_CONTROL_MASK | GDK_SHIFT_MASK, "page.save-as", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_p, GDK_CONTROL_MASK, "page.print", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_c, GDK_CONTROL_MASK | GDK_SHIFT_MASK, "page.copy-all", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_1, GDK_ALT_MASK, "page.change", "i", 1);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_2, GDK_ALT_MASK, "page.change", "i", 2);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_3, GDK_ALT_MASK, "page.change", "i", 3);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_4, GDK_ALT_MASK, "page.change", "i", 4);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_5, GDK_ALT_MASK, "page.change", "i", 5);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_6, GDK_ALT_MASK, "page.change", "i", 6);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_7, GDK_ALT_MASK, "page.change", "i", 7);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_8, GDK_ALT_MASK, "page.change", "i", 8);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_9, GDK_ALT_MASK, "page.change", "i", 9);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_n, GDK_CONTROL_MASK | GDK_SHIFT_MASK, "page.move-to-new-window", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_f, GDK_CONTROL_MASK, "page.begin-search", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_h, GDK_CONTROL_MASK, "page.begin-replace", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_F10, 0, "win.show-primary-menu", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_comma, GDK_CONTROL_MASK, "win.show-preferences", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_question, GDK_CONTROL_MASK, "win.alternate-help-overlay", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_t, GDK_CONTROL_MASK|GDK_SHIFT_MASK, "win.undo-close-page", NULL);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_plus, GDK_CONTROL_MASK, "page.zoom-in", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_KP_Add, GDK_CONTROL_MASK, "page.zoom-in", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_equal, GDK_CONTROL_MASK, "page.zoom-in", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_minus, GDK_CONTROL_MASK, "page.zoom-out", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_0, GDK_CONTROL_MASK, "page.zoom-one", NULL);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_KP_0, GDK_CONTROL_MASK, "page.zoom-one", NULL);

  _editor_window_class_actions_init (klass);

  g_type_ensure (EDITOR_TYPE_OPEN_POPOVER);
  g_type_ensure (EDITOR_TYPE_POSITION_LABEL);
  g_type_ensure (EDITOR_TYPE_STATUSBAR);
}

static void
editor_window_init (EditorWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->closed_items = g_array_new (FALSE, FALSE, sizeof (ClosedItem));
  g_array_set_clear_func (self->closed_items, closed_item_clear);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "win.undo-close-page", FALSE);

  g_signal_connect (self,
                    "notify::focus-widget",
                    G_CALLBACK (notify_focus_widget_cb),
                    NULL);

  gtk_window_set_title (GTK_WINDOW (self), _(PACKAGE_NAME));
  gtk_window_set_default_size (GTK_WINDOW (self), 700, 520);

  adw_status_page_set_icon_name (ADW_STATUS_PAGE (self->empty), APP_ID"-symbolic");

  self->settings = g_settings_new ("org.gnome.TextEditor");
  g_signal_connect_object (self->settings,
                           "changed::show-line-numbers",
                           G_CALLBACK (update_subtitle_visibility_cb),
                           self,
                           G_CONNECT_SWAPPED);
  g_signal_connect_object (self->settings,
                           "changed::keybindings",
                           G_CALLBACK (update_keybindings_cb),
                           self,
                           G_CONNECT_SWAPPED);
  update_keybindings_cb (self, NULL, self->settings);

  g_signal_connect_swapped (adw_tab_view_get_pages (self->tab_view),
                            "items-changed",
                            G_CALLBACK (editor_window_items_changed_cb),
                            self);
  g_signal_connect_swapped (self->tab_view,
                            "notify::selected-page",
                            G_CALLBACK (editor_window_notify_selected_page_cb),
                            self);

  self->page_signals = editor_signal_group_new (EDITOR_TYPE_PAGE);

  g_signal_connect_object (self->page_signals,
                           "bind",
                           G_CALLBACK (editor_window_page_bind_cb),
                           self,
                           G_CONNECT_SWAPPED);

  g_signal_connect_object (self->page_signals,
                           "unbind",
                           G_CALLBACK (editor_window_page_unbind_cb),
                           self,
                           G_CONNECT_SWAPPED);

  editor_signal_group_connect_object (self->page_signals,
                                      "notify::can-save",
                                      G_CALLBACK (editor_window_update_actions),
                                      self,
                                      G_CONNECT_SWAPPED);
  editor_signal_group_connect_object (self->page_signals,
                                      "notify::is-modified",
                                      G_CALLBACK (editor_window_update_actions),
                                      self,
                                      G_CONNECT_SWAPPED);
  editor_signal_group_connect_object (self->page_signals,
                                      "notify::can-discard",
                                      G_CALLBACK (update_subtitle_visibility_cb),
                                      self,
                                      G_CONNECT_SWAPPED);
  editor_signal_group_connect_object (self->page_signals,
                                      "notify::subtitle",
                                      G_CALLBACK (update_subtitle_visibility_cb),
                                      self,
                                      G_CONNECT_SWAPPED);
  editor_signal_group_connect_object (self->page_signals,
                                      "notify::language-name",
                                      G_CALLBACK (update_language_name_cb),
                                      self,
                                      G_CONNECT_SWAPPED);

  self->document_signals = editor_signal_group_new (EDITOR_TYPE_DOCUMENT);

  editor_signal_group_connect_object (self->document_signals,
                                      "cursor-moved",
                                      G_CALLBACK (editor_window_cursor_moved_cb),
                                      self,
                                      G_CONNECT_SWAPPED);

  self->page_bindings = editor_binding_group_new ();

  editor_binding_group_bind_full (self->page_bindings, "title",
                                  self, "title",
                                  G_BINDING_SYNC_CREATE,
                                  transform_window_title, NULL, NULL, NULL);
  editor_binding_group_bind_full (self->page_bindings, "indicator",
                                  self->indicator, "visible",
                                  G_BINDING_SYNC_CREATE,
                                  indicator_to_boolean, NULL, NULL, NULL);
  editor_binding_group_bind (self->page_bindings, "indicator",
                             self->indicator, "gicon",
                             G_BINDING_SYNC_CREATE);
  editor_binding_group_bind (self->page_bindings, "title",
                             self->title, "label",
                             G_BINDING_SYNC_CREATE);
  editor_binding_group_bind (self->page_bindings, "subtitle",
                             self->subtitle, "label",
                             G_BINDING_SYNC_CREATE);
  editor_binding_group_bind (self->page_bindings, "is-modified",
                             self->is_modified, "visible",
                             G_BINDING_SYNC_CREATE);

  _editor_window_dnd_init (self);

  _editor_window_actions_init (self);
}

EditorWindow *
_editor_window_new (void)
{
  g_autoptr(GtkWindowGroup) group = gtk_window_group_new ();
  EditorWindow *window;

  window = g_object_new (EDITOR_TYPE_WINDOW,
                         "application", EDITOR_APPLICATION_DEFAULT,
                         NULL);
  gtk_window_group_add_window (group, GTK_WINDOW (window));

  return window;
}

GList *
_editor_window_get_pages (EditorWindow *self)
{
  GQueue queue = G_QUEUE_INIT;
  guint n_pages;

  g_return_val_if_fail (EDITOR_IS_WINDOW (self), NULL);

  n_pages = editor_window_get_n_pages (self);

  for (guint i = 0; i < n_pages; i++)
    g_queue_push_tail (&queue, editor_window_get_nth_page (self, i));

  return g_steal_pointer (&queue.head);
}

static gboolean
modified_to_icon (GBinding     *binding,
                  const GValue *from_value,
                  GValue       *to_value,
                  gpointer      user_data)
{
  static GIcon *icon;

  if (icon == NULL)
    icon = g_themed_icon_new ("document-modified-symbolic");

  if (g_value_get_boolean (from_value))
    g_value_set_object (to_value, icon);

  return TRUE;
}

void
_editor_window_add_page (EditorWindow *self,
                         EditorPage   *page)
{
  AdwTabPage *tab_page;

  g_return_if_fail (EDITOR_IS_WINDOW (self));
  g_return_if_fail (EDITOR_IS_PAGE (page));

  tab_page = adw_tab_view_append (self->tab_view, GTK_WIDGET (page));

  g_object_bind_property (page, "title", tab_page, "title", G_BINDING_SYNC_CREATE);
  g_object_bind_property (page, "busy", tab_page, "loading", G_BINDING_SYNC_CREATE);
  g_object_bind_property (page, "indicator", tab_page, "indicator-icon", G_BINDING_SYNC_CREATE);
  g_object_bind_property_full (page, "is-modified",
                               tab_page, "icon",
                               G_BINDING_SYNC_CREATE,
                               modified_to_icon, NULL,
                               NULL, NULL);

  adw_tab_view_set_selected_page (self->tab_view, tab_page);
}

void
_editor_window_remove_page (EditorWindow *self,
                            EditorPage   *page)
{
  AdwTabPage *tab_page;

  g_return_if_fail (EDITOR_IS_WINDOW (self));
  g_return_if_fail (EDITOR_IS_PAGE (page));

  tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (page));
  adw_tab_view_close_page (self->tab_view, tab_page);

  if (self->visible_page == page)
    {
      editor_window_notify_selected_page_cb (self, NULL, self->tab_view);

      if (self->visible_page != NULL)
        editor_page_grab_focus (self->visible_page);
    }
}

/**
 * editor_window_get_visible_page:
 * @self: a #EditorWindow
 *
 * Gets the visible page for the window.
 *
 * Returns: (transfer none) (nullable): an #EditorPage or %NULL
 */
EditorPage *
editor_window_get_visible_page (EditorWindow *self)
{
  g_return_val_if_fail (EDITOR_IS_WINDOW (self), NULL);

  return self->visible_page;
}

/**
 * editor_window_set_visible_page:
 * @self: an #EditorWindow
 * @page: an #EditorPage
 *
 * Sets the currently visible page.
 */
void
editor_window_set_visible_page (EditorWindow *self,
                                EditorPage   *page)
{
  AdwTabPage *tab_page;

  g_return_if_fail (EDITOR_IS_WINDOW (self));
  g_return_if_fail (EDITOR_IS_PAGE (page));

  if ((tab_page = adw_tab_view_get_page (self->tab_view, GTK_WIDGET (page))))
    adw_tab_view_set_selected_page (self->tab_view, tab_page);
}

/**
 * editor_window_get_nth_page:
 * @self: a #EditorWindow
 * @nth: the page number starting from 0
 *
 * Gets the @nth page from the window.
 *
 * Returns: (transfer none) (nullable): an #EditorPage or %NULL
 */
EditorPage *
editor_window_get_nth_page (EditorWindow *self,
                            guint         nth)
{
  AdwTabPage *tab_page;

  g_return_val_if_fail (EDITOR_IS_WINDOW (self), NULL);

  if ((tab_page = adw_tab_view_get_nth_page (self->tab_view, nth)))
    return EDITOR_PAGE (adw_tab_page_get_child (tab_page));

  return NULL;
}

/**
 * editor_window_get_n_pages:
 * @self: a #EditorWindow
 *
 * Gets the number of pages in @self.
 *
 * Returns: the number of pages in the window
 */
guint
editor_window_get_n_pages (EditorWindow *self)
{
  g_return_val_if_fail (EDITOR_IS_WINDOW (self), 0);

  return adw_tab_view_get_n_pages (self->tab_view);
}

void
_editor_window_focus_search (EditorWindow *self)
{
  g_return_if_fail (EDITOR_IS_WINDOW (self));

  gtk_menu_button_popup (self->open_menu_button);
}
