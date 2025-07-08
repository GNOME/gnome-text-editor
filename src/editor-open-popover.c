/* editor-open-popover.c
 *
 * Copyright 2021 Christian Hergert <chergert@redhat.com>
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

#define G_LOG_DOMAIN "editor-open-popover"

#include "config.h"

#include "editor-application.h"
#include "editor-open-popover-private.h"
#include "editor-search-model.h"
#include "editor-session-private.h"
#include "editor-sidebar-item-private.h"
#include "editor-sidebar-row-private.h"
#include "editor-window-private.h"

struct _EditorOpenPopover
{
  GtkPopover         parent_instance;

  EditorSearchModel *model;

  GtkListView       *list_view;
  GtkNoSelection    *selection;
  GtkWidget         *box;
  GtkSearchEntry    *search_entry;
  GtkStack          *stack;
  GtkWidget         *empty;
  GtkScrolledWindow *recent;
};

G_DEFINE_FINAL_TYPE (EditorOpenPopover, editor_open_popover, GTK_TYPE_POPOVER)

enum {
  PROP_0,
  PROP_MODEL,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

GtkWidget *
_editor_open_popover_new (void)
{
  return g_object_new (EDITOR_TYPE_OPEN_POPOVER, NULL);
}

static void
on_items_changed (EditorOpenPopover *self,
                  guint              position,
                  guint              removed,
                  guint              added,
                  GListModel        *model)
{
  GtkWidget *visible_child;

  /* Handle disposal cleanly */
  if (self->stack == NULL)
    return;

  if (g_list_model_get_n_items (model) == 0)
    visible_child = GTK_WIDGET (self->empty);
  else
    visible_child = GTK_WIDGET (self->recent);

  /* Check against visible child because it's faster */
  if (visible_child != gtk_stack_get_visible_child (self->stack))
    gtk_stack_set_visible_child (self->stack, visible_child);
}

static void
popover_hide (GtkWidget  *widget,
              const char *action_name,
              GVariant   *param)
{
  GtkWidget *parent;

  g_assert (GTK_IS_POPOVER (widget));

  parent = gtk_widget_get_parent (widget);
  gtk_menu_button_popdown (GTK_MENU_BUTTON (parent));
}

static void
on_list_view_activate_cb (EditorOpenPopover *self,
                          guint              position,
                          GtkListView       *list_view)
{
  g_autoptr(EditorSidebarItem) item = NULL;
  GtkSelectionModel *model;
  EditorWindow *window;

  g_assert (EDITOR_IS_OPEN_POPOVER (self));
  g_assert (GTK_IS_LIST_VIEW (list_view));

  window = EDITOR_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));
  model = gtk_list_view_get_model (list_view);
  item = g_list_model_get_item (G_LIST_MODEL (model), position);

  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");
  _editor_sidebar_item_open (item, EDITOR_SESSION_DEFAULT, window);
  popover_hide (GTK_WIDGET (self), NULL, NULL);
}

static void
on_search_entry_changed_cb (EditorOpenPopover *self,
                            GtkSearchEntry    *search_entry)
{
  g_autofree char *casefold = NULL;
  const char *text;

  g_assert (EDITOR_IS_OPEN_POPOVER (self));
  g_assert (GTK_IS_SEARCH_ENTRY (search_entry));

  text = gtk_editable_get_text (GTK_EDITABLE (search_entry));

  if (text[0] != 0)
    casefold = g_utf8_casefold (text, -1);

  editor_search_model_set_search_text (self->model, casefold);
}

static void
on_search_entry_activate_cb (EditorOpenPopover *self,
                             GtkSearchEntry    *search_entry)
{
  GListModel *model;

  g_assert (EDITOR_IS_OPEN_POPOVER (self));
  g_assert (GTK_IS_SEARCH_ENTRY (search_entry));

  model = G_LIST_MODEL (gtk_list_view_get_model (self->list_view));

  if (g_list_model_get_n_items (model) > 0)
    {
      g_autoptr(EditorSidebarItem) item = g_list_model_get_item (model, 0);
      EditorWindow *window;

      gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");

      window = EDITOR_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));
      _editor_sidebar_item_open (item, EDITOR_SESSION_DEFAULT, window);

      popover_hide (GTK_WIDGET (self), NULL, NULL);
    }
}

static void
on_search_entry_stop_search_cb (EditorOpenPopover *self,
                                GtkSearchEntry    *search_entry)
{
  EditorWindow *window;

  g_assert (EDITOR_IS_OPEN_POPOVER (self));
  g_assert (GTK_IS_SEARCH_ENTRY (search_entry));

  window = EDITOR_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));

  gtk_popover_popdown (GTK_POPOVER (self));

  if (window != NULL)
    {
      EditorPage *page = editor_window_get_visible_page (window);

      if (page != NULL)
        gtk_widget_child_focus (GTK_WIDGET (page), GTK_DIR_TAB_FORWARD);
    }
}

static gboolean
editor_open_popover_has_rows (EditorOpenPopover *self)
{
  g_assert (EDITOR_IS_OPEN_POPOVER (self));

  return g_list_model_get_n_items (G_LIST_MODEL (gtk_list_view_get_model (self->list_view))) > 0;
}

static void
editor_open_popover_show (GtkWidget *widget)
{
  EditorOpenPopover *self = (EditorOpenPopover *)widget;
  GtkAdjustment *adj;

  g_assert (EDITOR_IS_OPEN_POPOVER (self));

  adj = gtk_scrolled_window_get_vadjustment (self->recent);
  gtk_adjustment_set_value (adj, 0);

  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");

  if (editor_open_popover_has_rows (self))
    gtk_list_view_scroll_to (self->list_view,
                             0,
                             GTK_LIST_SCROLL_NONE,
                             NULL);

  GTK_WIDGET_CLASS (editor_open_popover_parent_class)->show (widget);

  gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

static gboolean
on_search_key_pressed_cb (EditorOpenPopover     *self,
                          guint                  keyval,
                          guint                  keycode,
                          GdkModifierType        state,
                          GtkEventControllerKey *key)
{
  g_assert (EDITOR_IS_OPEN_POPOVER (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_KEY (key));

  if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down)
    {
      if (editor_open_popover_has_rows (self))
        gtk_list_view_scroll_to (self->list_view,
                                 0,
                                 GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT,
                                 NULL);
    }

  return FALSE;
}

static void
editor_open_popover_move_up (GtkWidget  *widget,
                             const char *action_name,
                             GVariant   *param)
{
  static GType GtkListItemWidget;
  EditorOpenPopover *self = (EditorOpenPopover *)widget;
  GtkWidget *focus;
  GtkRoot *root;

  g_assert (EDITOR_IS_OPEN_POPOVER (self));

  /*
   * This function just works around the fact we don't get a keynav-failed
   * when the GtkListView fails to move upwards from row 0.
   */

  if (GtkListItemWidget == G_TYPE_INVALID)
    {
      GtkListItemWidget = g_type_from_name ("GtkListItemWidget");
      g_assert (GtkListItemWidget != G_TYPE_INVALID);
    }

  root = gtk_widget_get_root (widget);
  if (!(focus = gtk_root_get_focus (root)))
    return;

  if (!G_TYPE_CHECK_INSTANCE_TYPE (focus, GtkListItemWidget))
    {
      if (!(focus = gtk_widget_get_ancestor (focus, GtkListItemWidget)))
        return;
    }

  for (GtkWidget *child = gtk_widget_get_first_child (focus);
       child != NULL;
       child = gtk_widget_get_next_sibling (child))
    {
      if (EDITOR_IS_SIDEBAR_ROW (child))
        {
          guint position = _editor_sidebar_row_get_position (EDITOR_SIDEBAR_ROW (child));

          if (position == GTK_INVALID_LIST_POSITION)
            return;

          if (position == 0)
            gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
          else
            gtk_list_view_scroll_to (self->list_view,
                                     position - 1,
                                     GTK_LIST_SCROLL_FOCUS,
                                     NULL);

          return;
        }
    }

}

static void
editor_open_popover_dispose (GObject *object)
{
  EditorOpenPopover *self = (EditorOpenPopover *)object;

  g_clear_pointer (&self->box, gtk_widget_unparent);
  g_clear_object (&self->model);

  G_OBJECT_CLASS (editor_open_popover_parent_class)->dispose (object);
}

static void
editor_open_popover_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  EditorOpenPopover *self = EDITOR_OPEN_POPOVER (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, _editor_open_popover_get_model (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_open_popover_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  EditorOpenPopover *self = EDITOR_OPEN_POPOVER (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      _editor_open_popover_set_model (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_open_popover_class_init (EditorOpenPopoverClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_open_popover_dispose;
  object_class->get_property = editor_open_popover_get_property;
  object_class->set_property = editor_open_popover_set_property;

  widget_class->show = editor_open_popover_show;

  properties [PROP_MODEL] =
    g_param_spec_object ("model",
                         "Model",
                         "The model to be loaded",
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-open-popover.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, box);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, empty);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, search_entry);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, selection);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, list_view);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, recent);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenPopover, stack);
  gtk_widget_class_bind_template_callback (widget_class, on_list_view_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_stop_search_cb);

  gtk_widget_class_install_action (widget_class, "move-up", NULL, editor_open_popover_move_up);

  g_type_ensure (EDITOR_TYPE_SEARCH_MODEL);
  g_type_ensure (EDITOR_TYPE_SIDEBAR_MODEL);
  g_type_ensure (EDITOR_TYPE_SIDEBAR_ROW);
}

static void
editor_open_popover_init (EditorOpenPopover *self)
{
  GtkEventController *controller;

  gtk_widget_init_template (GTK_WIDGET (self));

  controller = gtk_event_controller_key_new ();
  g_signal_connect_object (controller,
                           "key-pressed",
                           G_CALLBACK (on_search_key_pressed_cb),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
  gtk_widget_add_controller (GTK_WIDGET (self->search_entry), controller);

  self->model = editor_search_model_new ();
  g_signal_connect_object (self->model,
                           "items-changed",
                           G_CALLBACK (on_items_changed),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_no_selection_set_model (self->selection, G_LIST_MODEL (self->model));
}

GListModel *
_editor_open_popover_get_model (EditorOpenPopover *self)
{
  g_return_val_if_fail (EDITOR_IS_OPEN_POPOVER (self), NULL);

  return G_LIST_MODEL (self->model);
}

void
_editor_open_popover_set_model (EditorOpenPopover *self,
                                GListModel        *model)
{
  g_return_if_fail (EDITOR_IS_OPEN_POPOVER (self));
  g_return_if_fail (!model || G_IS_LIST_MODEL (model));

  editor_search_model_set_model (self->model, model);
}
