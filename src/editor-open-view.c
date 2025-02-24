/* editor-open-view.c
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

#define G_LOG_DOMAIN "editor-open-view"

#include "config.h"

#include "editor-application.h"
#include "editor-open-view.h"
#include "editor-session-private.h"
#include "editor-sidebar-item-private.h"
#include "editor-sidebar-row-private.h"
#include "editor-window-private.h"

struct _EditorOpenView
{
  GtkWidget           parent_instance;

  GListModel         *model;
  GListModel         *filtered_model;
  GListModel         *sorted_model;

  AdwMultiLayoutView *multi_layout;
  GtkListView        *list_view;
  GtkSearchEntry     *search_entry;
  GtkStack           *stack;
  GtkWidget          *empty;
  GtkScrolledWindow  *recent;
};

G_DEFINE_TYPE (EditorOpenView, editor_open_view, GTK_TYPE_WIDGET)

enum {
  PROP_0,
  PROP_MODEL,
  PROP_NARROW,
  N_PROPS
};

enum {
  CLOSE,
  N_SIGNALS
};

static GParamSpec *properties [N_PROPS];
static guint signals [N_SIGNALS];

GtkWidget *
_editor_open_view_new (void)
{
  return g_object_new (EDITOR_TYPE_OPEN_VIEW, NULL);
}

static void
on_items_changed (EditorOpenView *self,
                  guint           position,
                  guint           removed,
                  guint           added,
                  GListModel     *model)
{
  GtkWidget *visible_child;

  if (g_list_model_get_n_items (model) == 0)
    visible_child = GTK_WIDGET (self->empty);
  else
    visible_child = GTK_WIDGET (self->recent);

  /* Check against visible child because it's faster */
  if (visible_child != gtk_stack_get_visible_child (self->stack))
    gtk_stack_set_visible_child (self->stack, visible_child);
}

static void
set_model (EditorOpenView *self,
           GListModel        *model)
{
  g_autoptr(GtkSelectionModel) selection = NULL;
  GtkSelectionModel *previous;

  g_assert (EDITOR_IS_OPEN_VIEW (self));
  g_assert (G_IS_LIST_MODEL (model));

  if ((previous = gtk_list_view_get_model (self->list_view)))
    g_signal_handlers_disconnect_by_func (previous,
                                          G_CALLBACK (on_items_changed),
                                          self);

  selection = g_object_new (GTK_TYPE_NO_SELECTION,
                            "model", model,
                            NULL);
  g_signal_connect_object (selection,
                           "items-changed",
                           G_CALLBACK (on_items_changed),
                           self,
                           G_CONNECT_SWAPPED);
  gtk_list_view_set_model (self->list_view, selection);

  on_items_changed (self, 0, 0, 0, G_LIST_MODEL (selection));
}

static void
on_list_view_activate_cb (EditorOpenView *self,
                          guint           position,
                          GtkListView    *list_view)
{
  g_autoptr(EditorSidebarItem) item = NULL;
  GtkSelectionModel *model;
  EditorWindow *window;

  g_assert (EDITOR_IS_OPEN_VIEW (self));
  g_assert (GTK_IS_LIST_VIEW (list_view));

  window = EDITOR_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));
  model = gtk_list_view_get_model (list_view);
  item = g_list_model_get_item (G_LIST_MODEL (model), position);

  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");
  _editor_sidebar_item_open (item, EDITOR_SESSION_DEFAULT, window);

  g_signal_emit (self, signals[CLOSE], 0);
}

static gboolean
editor_sidebar_filter_func_cb (gpointer itemptr,
                               gpointer user_data)
{
  EditorSidebarItem *item = itemptr;
  const char *search = user_data;

  return _editor_sidebar_item_matches (item, search);
}

static void
on_search_entry_changed_cb (EditorOpenView *self,
                            GtkSearchEntry *search_entry)
{
  g_autoptr(GtkFilterListModel) filter = NULL;
  g_autoptr(GtkSortListModel) sorted = NULL;
  g_autoptr(GtkSelectionModel) selection = NULL;
  GListModel *model;
  const char *text;

  g_assert (EDITOR_IS_OPEN_VIEW (self));
  g_assert (GTK_IS_SEARCH_ENTRY (search_entry));

  if (self->model == NULL)
    return;

  text = gtk_editable_get_text (GTK_EDITABLE (search_entry));

  if (text == NULL || text[0] == 0)
    {
      model = G_LIST_MODEL (self->model);
    }
  else
    {
      g_autofree gchar *text_fold = g_utf8_casefold (text, -1);
      g_autoptr(GtkCustomFilter) custom = NULL;
      g_autoptr(GtkNumericSorter) sorter = NULL;

      custom = gtk_custom_filter_new (editor_sidebar_filter_func_cb,
                                      g_steal_pointer (&text_fold),
                                      g_free);
      filter = gtk_filter_list_model_new (g_object_ref (G_LIST_MODEL (self->model)),
                                          g_object_ref (GTK_FILTER (custom)));
      sorter = gtk_numeric_sorter_new (gtk_property_expression_new (EDITOR_TYPE_SIDEBAR_ITEM, NULL, "score"));
      gtk_numeric_sorter_set_sort_order (sorter, GTK_SORT_ASCENDING);
      sorted = gtk_sort_list_model_new (g_object_ref (G_LIST_MODEL (filter)),
                                        g_object_ref (GTK_SORTER (sorter)));

      model = G_LIST_MODEL (sorted);
    }

  g_assert (model != NULL);
  g_assert (G_IS_LIST_MODEL (model));

  set_model (self, model);

  g_set_object (&self->filtered_model, model);
}

static void
on_search_entry_activate_cb (EditorOpenView *self,
                             GtkSearchEntry *search_entry)
{
  GListModel *model;

  g_assert (EDITOR_IS_OPEN_VIEW (self));
  g_assert (GTK_IS_SEARCH_ENTRY (search_entry));

  model = G_LIST_MODEL (gtk_list_view_get_model (self->list_view));

  if (g_list_model_get_n_items (model) > 0)
    {
      g_autoptr(EditorSidebarItem) item = g_list_model_get_item (model, 0);
      EditorWindow *window;

      gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");

      window = EDITOR_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));
      _editor_sidebar_item_open (item, EDITOR_SESSION_DEFAULT, window);

      g_signal_emit (self, signals[CLOSE], 0);
    }
}

static void
on_search_entry_stop_search_cb (EditorOpenView *self,
                                GtkSearchEntry *search_entry)
{
  EditorWindow *window;

  g_assert (EDITOR_IS_OPEN_VIEW (self));
  g_assert (GTK_IS_SEARCH_ENTRY (search_entry));

  window = EDITOR_WINDOW (gtk_widget_get_root (GTK_WIDGET (self)));

  g_signal_emit (self, signals[CLOSE], 0);

  if (window != NULL)
    {
      EditorPage *page = editor_window_get_visible_page (window);

      if (page != NULL)
        gtk_widget_child_focus (GTK_WIDGET (page), GTK_DIR_TAB_FORWARD);
    }
}

static gboolean
editor_open_view_has_rows (EditorOpenView *self)
{
  g_assert (EDITOR_IS_OPEN_VIEW (self));

  return g_list_model_get_n_items (G_LIST_MODEL (gtk_list_view_get_model (self->list_view))) > 0;
}

static void
editor_open_view_show (GtkWidget *widget)
{
  EditorOpenView *self = (EditorOpenView *)widget;
  GtkAdjustment *adj;

  g_assert (EDITOR_IS_OPEN_VIEW (self));

  adj = gtk_scrolled_window_get_vadjustment (self->recent);
  gtk_adjustment_set_value (adj, 0);

  gtk_editable_set_text (GTK_EDITABLE (self->search_entry), "");

  if (editor_open_view_has_rows (self))
    gtk_list_view_scroll_to (self->list_view,
                             0,
                             GTK_LIST_SCROLL_NONE,
                             NULL);

  GTK_WIDGET_CLASS (editor_open_view_parent_class)->show (widget);

  gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

static gboolean
on_search_key_pressed_cb (EditorOpenView        *self,
                          guint                  keyval,
                          guint                  keycode,
                          GdkModifierType        state,
                          GtkEventControllerKey *key)
{
  g_assert (EDITOR_IS_OPEN_VIEW (self));
  g_assert (GTK_IS_EVENT_CONTROLLER_KEY (key));

  if (keyval == GDK_KEY_Down || keyval == GDK_KEY_KP_Down)
    {
      if (editor_open_view_has_rows (self))
        gtk_list_view_scroll_to (self->list_view,
                                 0,
                                 GTK_LIST_SCROLL_FOCUS | GTK_LIST_SCROLL_SELECT,
                                 NULL);
    }

  return FALSE;
}

static void
editor_open_view_move_up (GtkWidget  *widget,
                          const char *action_name,
                          GVariant   *param)
{
  static GType GtkListItemWidget;
  EditorOpenView *self = (EditorOpenView *)widget;
  GtkWidget *focus;
  GtkRoot *root;

  g_assert (EDITOR_IS_OPEN_VIEW (self));

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

static gboolean
editor_open_view_grab_focus (GtkWidget *widget)
{
  EditorOpenView *self = EDITOR_OPEN_VIEW (widget);

  return gtk_widget_grab_focus (GTK_WIDGET (self->search_entry));
}

static void
editor_open_view_dispose (GObject *object)
{
  EditorOpenView *self = (EditorOpenView *)object;
  GtkWidget *child;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&self->model);
  g_clear_object (&self->sorted_model);
  g_clear_object (&self->filtered_model);

  G_OBJECT_CLASS (editor_open_view_parent_class)->dispose (object);
}

static void
editor_open_view_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  EditorOpenView *self = EDITOR_OPEN_VIEW (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, _editor_open_view_get_model (self));
      break;

    case PROP_NARROW:
      g_value_set_boolean (value, _editor_open_view_get_narrow (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_open_view_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  EditorOpenView *self = EDITOR_OPEN_VIEW (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      _editor_open_view_set_model (self, g_value_get_object (value));
      break;

    case PROP_NARROW:
      _editor_open_view_set_narrow (self, g_value_get_boolean (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_open_view_class_init (EditorOpenViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_open_view_dispose;
  object_class->get_property = editor_open_view_get_property;
  object_class->set_property = editor_open_view_set_property;

  widget_class->show = editor_open_view_show;
  widget_class->grab_focus = editor_open_view_grab_focus;

  properties [PROP_MODEL] =
    g_param_spec_object ("model",
                         "Model",
                         "The model to be loaded",
                         G_TYPE_LIST_MODEL,
                         (G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS));

  properties[PROP_NARROW] =
    g_param_spec_boolean ("narrow", NULL, NULL,
                          FALSE,
                          (G_PARAM_READWRITE |
                           G_PARAM_EXPLICIT_NOTIFY |
                           G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  signals[CLOSE] =
    g_signal_new ("close",
                  G_TYPE_FROM_CLASS (klass),
                  G_SIGNAL_RUN_LAST,
                  0,
                  NULL, NULL,
                  NULL,
                  G_TYPE_NONE, 0);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-open-view.ui");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "openview");
  gtk_widget_class_bind_template_child (widget_class, EditorOpenView, empty);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenView, search_entry);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenView, list_view);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenView, multi_layout);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenView, recent);
  gtk_widget_class_bind_template_child (widget_class, EditorOpenView, stack);
  gtk_widget_class_bind_template_callback (widget_class, on_list_view_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_search_entry_stop_search_cb);

  gtk_widget_class_install_action (widget_class, "move-up", NULL, editor_open_view_move_up);

  g_type_ensure (EDITOR_TYPE_SIDEBAR_MODEL);
  g_type_ensure (EDITOR_TYPE_SIDEBAR_ROW);
}

static void
editor_open_view_init (EditorOpenView *self)
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
}

GListModel *
_editor_open_view_get_model (EditorOpenView *self)
{
  return self->model;
}

void
_editor_open_view_set_model (EditorOpenView *self,
                             GListModel     *model)
{
  g_return_if_fail (EDITOR_IS_OPEN_VIEW (self));
  g_return_if_fail (!model || G_IS_LIST_MODEL (model));

  if (g_set_object (&self->model, model))
    {
      g_autoptr(GtkSelectionModel) selection = NULL;

      g_clear_object (&self->filtered_model);
      g_clear_object (&self->sorted_model);

      set_model (self, model);

      g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_MODEL]);
    }
}

gboolean
_editor_open_view_get_narrow (EditorOpenView *self)
{
  g_return_val_if_fail (EDITOR_IS_OPEN_VIEW (self), FALSE);

  return g_strcmp0 ("narrow", adw_multi_layout_view_get_layout_name (self->multi_layout)) == 0;
}

void
_editor_open_view_set_narrow (EditorOpenView *self,
                              gboolean        narrow)
{
  g_return_if_fail (EDITOR_IS_OPEN_VIEW (self));

  if (narrow == _editor_open_view_get_narrow (self))
    return;

  adw_multi_layout_view_set_layout_name (self->multi_layout,
                                         narrow ? "narrow" : "wide");
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_NARROW]);
}
