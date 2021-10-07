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
#include "editor-preferences-font.h"
#include "editor-preferences-radio.h"
#include "editor-preferences-spin.h"
#include "editor-preferences-switch.h"
#include "editor-save-changes-dialog-private.h"
#include "editor-session-private.h"
#include "editor-theme-selector-private.h"
#include "editor-utils-private.h"
#include "editor-window-private.h"

G_DEFINE_TYPE (EditorWindow, editor_window, ADW_TYPE_APPLICATION_WINDOW)

enum {
  PROP_0,
  PROP_VISIBLE_PAGE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

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
  gtk_widget_set_visible (GTK_WIDGET (self->is_modified), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->subtitle), FALSE);
  gtk_widget_set_visible (GTK_WIDGET (self->position_box),
                          page && g_settings_get_boolean (self->settings, "show-line-numbers"));

  self->visible_page = page;

  editor_binding_group_set_source (self->page_bindings, page);
  editor_signal_group_set_target (self->page_signals, page);

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

static void
editor_window_constructed (GObject *object)
{
  EditorWindow *self = (EditorWindow *)object;
  GtkSourceStyleSchemeManager *sm;
  const char * const *scheme_ids;
  GtkApplication *app;
  EditorSession *session;
  GtkPopover *popover;
  GMenu *options_menu;
  GMenu *primary_menu;

  g_assert (EDITOR_IS_WINDOW (self));

  G_OBJECT_CLASS (editor_window_parent_class)->constructed (object);

  app = GTK_APPLICATION (EDITOR_APPLICATION_DEFAULT);
  session = editor_application_get_session (EDITOR_APPLICATION_DEFAULT);

  /* Set the recents list for the open popover */
  g_object_bind_property (session, "recents",
                          self->open_menu_popover, "model",
                          G_BINDING_SYNC_CREATE);

  primary_menu = gtk_application_get_menu_by_id (GTK_APPLICATION (app), "primary-menu");
  gtk_menu_button_set_menu_model (self->primary_menu, G_MENU_MODEL (primary_menu));

  /* The primary menu has some custom widgets added to it */
  popover = gtk_menu_button_get_popover (self->primary_menu);
  gtk_popover_menu_add_child (GTK_POPOVER_MENU (popover),
                              _editor_theme_selector_new (),
                              "theme");

  options_menu = gtk_application_get_menu_by_id (GTK_APPLICATION (app), "options-menu");
  gtk_menu_button_set_menu_model (self->options_menu, G_MENU_MODEL (options_menu));

  /* Populate schemes for preferences */
  sm = gtk_source_style_scheme_manager_get_default ();
  if ((scheme_ids = gtk_source_style_scheme_manager_get_scheme_ids (sm)))
    {
      GtkCheckButton *group = NULL;

      for (guint i = 0; scheme_ids[i]; i++)
        {
          GtkSourceStyleScheme *scheme = gtk_source_style_scheme_manager_get_scheme (sm, scheme_ids[i]);
          const char *name = gtk_source_style_scheme_get_name (scheme);
          GtkWidget *radio;
          GtkWidget *w;

          radio = g_object_new (GTK_TYPE_CHECK_BUTTON,
                                "can-focus", FALSE,
                                "valign", GTK_ALIGN_CENTER,
                                "action-name", "app.style-scheme",
                                "action-target", g_variant_new_string (scheme_ids[i]),
                                NULL);
          w = adw_action_row_new ();
          adw_preferences_row_set_title (ADW_PREFERENCES_ROW (w), name);
          adw_action_row_add_prefix (ADW_ACTION_ROW (w), radio);
          adw_action_row_set_activatable_widget (ADW_ACTION_ROW (w), radio);
          adw_preferences_group_add (self->scheme_group, w);

          if (group == NULL)
            group = GTK_CHECK_BUTTON (radio);
          else
            gtk_check_button_set_group (GTK_CHECK_BUTTON (radio), group);
        }
    }
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

  if ((epage = EDITOR_PAGE (adw_tab_page_get_child (page))))
    {
      epage->close_requested = TRUE;

      if (_editor_window_request_close_page (self, epage))
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
  g_return_val_if_fail (EDITOR_IS_WINDOW (self), FALSE);
  g_return_val_if_fail (EDITOR_IS_PAGE (page), FALSE);

  /* If this document has changes, then request the user to save them. */
  if (editor_page_get_is_modified (page))
    {
      g_autoptr(GPtrArray) pages = g_ptr_array_new_with_free_func (g_object_unref);
      g_ptr_array_add (pages, g_object_ref (page));
      _editor_save_changes_dialog_run_async (GTK_WINDOW (self),
                                             pages,
                                             NULL,
                                             editor_window_actions_close_page_confirm_cb,
                                             g_ptr_array_ref (pages));
      return FALSE;
    }

  return TRUE;
}

static void
on_notify_reveal_flap_cb (EditorWindow *self,
                          GParamSpec   *pspec,
                          AdwFlap      *flap)
{
  g_assert (EDITOR_IS_WINDOW (self));
  g_assert (ADW_IS_FLAP (flap));

  if (!adw_flap_get_reveal_flap (flap))
    adw_flap_set_locked (flap, TRUE);
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
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, flap);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, is_modified);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, open_menu_button);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, open_menu_popover);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, options_menu);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, pages);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, position_box);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, position_label);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, primary_menu);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, scheme_group);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, stack);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, subtitle);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, tab_bar);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, tab_view);
  gtk_widget_class_bind_template_child (widget_class, EditorWindow, title);

  gtk_widget_class_bind_template_callback (widget_class, on_tab_view_close_page_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_notify_reveal_flap_cb);

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

  _editor_window_class_actions_init (klass);

  g_type_ensure (EDITOR_TYPE_OPEN_POPOVER);
  g_type_ensure (EDITOR_TYPE_POSITION_LABEL);
  g_type_ensure (EDITOR_TYPE_PREFERENCES_FONT);
  g_type_ensure (EDITOR_TYPE_PREFERENCES_SPIN);
  g_type_ensure (EDITOR_TYPE_PREFERENCES_SWITCH);
}

static void
editor_window_init (EditorWindow *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_window_set_title (GTK_WINDOW (self), _(PACKAGE_NAME));
  gtk_window_set_default_size (GTK_WINDOW (self), 700, 520);

  self->settings = g_settings_new ("org.gnome.TextEditor");
  g_signal_connect_object (self->settings,
                           "changed::show-line-numbers",
                           G_CALLBACK (update_subtitle_visibility_cb),
                           self,
                           G_CONNECT_SWAPPED);

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

  self->document_signals = editor_signal_group_new (EDITOR_TYPE_DOCUMENT);

  editor_signal_group_connect_object (self->document_signals,
                                      "cursor-moved",
                                      G_CALLBACK (editor_window_cursor_moved_cb),
                                      self,
                                      G_CONNECT_SWAPPED);

  self->page_bindings = editor_binding_group_new ();

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
  return g_object_new (EDITOR_TYPE_WINDOW,
                       "application", EDITOR_APPLICATION_DEFAULT,
                       NULL);
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
