/* editor-tab.c
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

#define G_LOG_DOMAIN "editor-tab"

#include "config.h"

#include "editor-application.h"
#include "editor-page-private.h"
#include "editor-session-private.h"
#include "editor-tab-private.h"
#include "editor-window-private.h"

enum {
  PROP_0,
  PROP_PAGE,
  N_PROPS
};

G_DEFINE_TYPE (EditorTab, editor_tab, GTK_TYPE_BIN)

static GParamSpec *properties [N_PROPS];

static void
editor_tab_page_notify_title_cb (EditorTab  *self,
                                 GParamSpec *pspec,
                                 EditorPage *page)
{
  g_autofree gchar *title = NULL;

  g_assert (EDITOR_IS_TAB (self));
  g_assert (EDITOR_IS_PAGE (page));
  g_assert (GTK_IS_LABEL (self->title));

  title = editor_page_dup_title (page);
  gtk_label_set_label (self->title, title);
}

static void
editor_tab_update_stack (EditorTab *self)
{
  GtkWidget *child;

  g_assert (EDITOR_IS_TAB (self));

  if (editor_page_get_busy (self->page))
    child = GTK_WIDGET (self->spinner);
  else if (editor_page_get_is_modified (self->page))
    child = GTK_WIDGET (self->is_modified);
  else
    child = GTK_WIDGET (self->empty);

  gtk_stack_set_visible_child (self->stack, child);

  if (child == GTK_WIDGET (self->spinner))
    gtk_spinner_start (self->spinner);
  else
    gtk_spinner_stop (self->spinner);
}

static void
editor_tab_page_notify_is_modified_cb (EditorTab  *self,
                                       GParamSpec *pspec,
                                       EditorPage *page)
{
  g_assert (EDITOR_IS_TAB (self));
  g_assert (EDITOR_IS_PAGE (page));
  g_assert (GTK_IS_LABEL (self->empty));
  g_assert (GTK_IS_LABEL (self->is_modified));
  g_assert (GTK_IS_STACK (self->stack));

  editor_tab_update_stack (self);
}

static void
editor_tab_page_notify_busy_cb (EditorTab  *self,
                                GParamSpec *pspec,
                                EditorPage *page)
{
  g_assert (EDITOR_IS_TAB (self));
  g_assert (EDITOR_IS_PAGE (page));

  editor_tab_update_stack (self);
}

static void
editor_tab_set_page (EditorTab  *self,
                     EditorPage *page)
{
  g_assert (EDITOR_IS_TAB (self));
  g_assert (!page || EDITOR_IS_PAGE (page));

  if (g_set_weak_pointer (&self->page, page))
    {
      g_signal_connect_object (page,
                               "notify::busy",
                               G_CALLBACK (editor_tab_page_notify_busy_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (page,
                               "notify::title",
                               G_CALLBACK (editor_tab_page_notify_title_cb),
                               self,
                               G_CONNECT_SWAPPED);
      g_signal_connect_object (page,
                               "notify::is-modified",
                               G_CALLBACK (editor_tab_page_notify_is_modified_cb),
                               self,
                               G_CONNECT_SWAPPED);

      editor_tab_page_notify_busy_cb (self, NULL, page);
      editor_tab_page_notify_title_cb (self, NULL, page);
      editor_tab_page_notify_is_modified_cb (self, NULL, page);
    }
}

static void
click_gesture_pressed_cb (EditorTab            *self,
                          int                   n_press,
                          double                x,
                          double                y,
                          GtkGestureMultiPress *click)
{
  gint button;

  g_assert (EDITOR_IS_TAB (self));
  g_assert (GTK_IS_GESTURE_MULTI_PRESS (click));

  if (self->page == NULL)
    return;

  button = gtk_gesture_single_get_current_button (GTK_GESTURE_SINGLE (click));

  switch (button)
    {
    case 2:
      editor_session_remove_page (EDITOR_SESSION_DEFAULT, self->page);
      break;

    case 3:
      if (self->menu_popover == NULL)
        {
          GtkApplication *app = GTK_APPLICATION (EDITOR_APPLICATION_DEFAULT);
          GMenu *menu = gtk_application_get_menu_by_id (app, "tab-menu");
          GtkWidget *popover = gtk_popover_new_from_model (GTK_WIDGET (self), G_MENU_MODEL (menu));

          self->menu_popover = GTK_POPOVER (popover);
          gtk_popover_set_position (self->menu_popover, GTK_POS_BOTTOM);
          gtk_widget_set_halign (GTK_WIDGET (self->menu_popover), GTK_ALIGN_CENTER);
        }

      _editor_tab_actions_update (self);
      gtk_popover_popup (GTK_POPOVER (self->menu_popover));

      break;

    default:
      break;
    }
}

static void
editor_tab_dispose (GObject *object)
{
  EditorTab *self = (EditorTab *)object;

  g_clear_weak_pointer (&self->page);

  G_OBJECT_CLASS (editor_tab_parent_class)->dispose (object);
}

static void
editor_tab_get_property (GObject    *object,
                         guint       prop_id,
                         GValue     *value,
                         GParamSpec *pspec)
{
  EditorTab *self = EDITOR_TAB (object);

  switch (prop_id)
    {
    case PROP_PAGE:
      g_value_set_object (value, editor_tab_get_page (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_tab_set_property (GObject      *object,
                         guint         prop_id,
                         const GValue *value,
                         GParamSpec   *pspec)
{
  EditorTab *self = EDITOR_TAB (object);

  switch (prop_id)
    {
    case PROP_PAGE:
      editor_tab_set_page (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_tab_class_init (EditorTabClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_tab_dispose;
  object_class->get_property = editor_tab_get_property;
  object_class->set_property = editor_tab_set_property;

  properties [PROP_PAGE] =
    g_param_spec_object ("page",
                         "Page",
                         "The page for the tab",
                         EDITOR_TYPE_PAGE,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));
  
  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-tab.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorTab, close_button);
  gtk_widget_class_bind_template_child (widget_class, EditorTab, empty);
  gtk_widget_class_bind_template_child (widget_class, EditorTab, event_box);
  gtk_widget_class_bind_template_child (widget_class, EditorTab, is_modified);
  gtk_widget_class_bind_template_child (widget_class, EditorTab, spinner);
  gtk_widget_class_bind_template_child (widget_class, EditorTab, stack);
  gtk_widget_class_bind_template_child (widget_class, EditorTab, title);

  _editor_tab_class_actions_init (klass);
}

static void
editor_tab_init (EditorTab *self)
{
  GtkGesture *gesture;

  gtk_widget_init_template (GTK_WIDGET (self));

  _editor_tab_actions_init (self);

  gtk_widget_set_hexpand (GTK_WIDGET (self), TRUE);

  gesture = gtk_gesture_multi_press_new (GTK_WIDGET (self->event_box));
  gtk_gesture_single_set_button (GTK_GESTURE_SINGLE (gesture), 0);
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture), GTK_PHASE_CAPTURE);
  g_signal_connect_object (gesture,
                           "pressed",
                           G_CALLBACK (click_gesture_pressed_cb),
                           self,
                           G_CONNECT_SWAPPED);
}

EditorTab *
_editor_tab_new (EditorPage *page)
{
  g_return_val_if_fail (EDITOR_IS_PAGE (page), NULL);

  return g_object_new (EDITOR_TYPE_TAB,
                       "page", page,
                       "visible", TRUE,
                       NULL);
}

/**
 * editor_tab_get_page:
 * @self: a #EditorTab
 *
 * Gets the page the tab represents.
 *
 * Returns: (transfer none): an #EditorTab
 */
EditorPage *
editor_tab_get_page (EditorTab *self)
{
  g_return_val_if_fail (EDITOR_IS_TAB (self), NULL);

  return self->page;
}
