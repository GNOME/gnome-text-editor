/*
 * editor-fullscreen-box.c
 *
 * Copyright © 2021 Purism SPC
 * Copyright © 2025 Christian Hergert <chergert@redhat.com>
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

#include "config.h"

#include "editor-fullscreen-box.h"

#include <adwaita.h>

#define FULLSCREEN_HIDE_DELAY 300
#define FORCED_REVEAL_DELAY  1500
#define SHOW_HEADERBAR_DISTANCE_PX 5

struct _EditorFullscreenBox {
  GtkWidget parent_instance;

  AdwToolbarView *toolbar_view;

  gboolean fullscreen;
  gboolean autohide;

  guint timeout_id;
  guint forced_reveal_id;

  GtkWidget *last_focus;
  gdouble last_y;
  gboolean is_touch;

  GList *headers;
};

static void buildable_iface_init (GtkBuildableIface *iface);

G_DEFINE_FINAL_TYPE_WITH_CODE (EditorFullscreenBox, editor_fullscreen_box, GTK_TYPE_WIDGET,
                               G_IMPLEMENT_INTERFACE (GTK_TYPE_BUILDABLE, buildable_iface_init))

enum {
  PROP_0,
  PROP_FULLSCREEN,
  PROP_AUTOHIDE,
  PROP_CONTENT,
  PROP_REVEALED,
  LAST_PROP
};

static GParamSpec *props[LAST_PROP];
static GtkBuildableIface *parent_buildable;

static void
show_ui (EditorFullscreenBox *self)
{
  g_clear_handle_id (&self->timeout_id, g_source_remove);

  adw_toolbar_view_set_reveal_top_bars (self->toolbar_view, TRUE);
  adw_toolbar_view_set_reveal_bottom_bars (self->toolbar_view, TRUE);
}

static void
hide_ui (EditorFullscreenBox *self)
{
  g_clear_handle_id (&self->timeout_id, g_source_remove);

  if (!self->fullscreen)
    return;

  adw_toolbar_view_set_reveal_top_bars (self->toolbar_view, FALSE);
  adw_toolbar_view_set_reveal_bottom_bars (self->toolbar_view, FALSE);

  gtk_widget_grab_focus (GTK_WIDGET (self->toolbar_view));
}

static void
hide_timeout_cb (EditorFullscreenBox *self)
{
  self->timeout_id = 0;

  hide_ui (self);
}

static void
start_hide_timeout (EditorFullscreenBox *self)
{
  if (!adw_toolbar_view_get_reveal_top_bars (self->toolbar_view))
    return;

  if (self->timeout_id)
    return;

  self->timeout_id = g_timeout_add_once (FULLSCREEN_HIDE_DELAY,
                                         (GSourceOnceFunc)hide_timeout_cb,
                                         self);
}

static gboolean
is_descendant_of (GtkWidget *widget,
                  GtkWidget *target)
{
  GtkWidget *parent;

  if (!widget)
    return FALSE;

  if (widget == target)
    return TRUE;

  parent = widget;

  while (parent && parent != target)
    parent = gtk_widget_get_parent (parent);

  return parent == target;
}

static double
get_titlebar_area_height (EditorFullscreenBox *self)
{
  double height = adw_toolbar_view_get_top_bar_height (self->toolbar_view);

  return MAX (height, SHOW_HEADERBAR_DISTANCE_PX);
}

static gboolean
is_focusing_titlebar (EditorFullscreenBox *self)
{
  GList *l;

  for (l = self->headers; l; l = l->next)
    if (is_descendant_of (self->last_focus, l->data))
      return TRUE;

  return FALSE;
}

static void
update (EditorFullscreenBox *self,
        gboolean             hide_immediately)
{
  if (!self->autohide || !self->fullscreen)
    return;

  if (self->forced_reveal_id) {
    if (self->last_y <= get_titlebar_area_height (self))
      g_clear_handle_id (&self->forced_reveal_id, g_source_remove);
    else
      return;
  }

  if (!self->is_touch &&
      self->last_y <= get_titlebar_area_height (self)) {
    show_ui (self);
    return;
  }

  if (self->last_focus && is_focusing_titlebar (self))
    show_ui (self);
  else if (hide_immediately)
    hide_ui (self);
  else
    start_hide_timeout (self);
}

static void
motion_cb (EditorFullscreenBox *self,
           double               x,
           double               y)
{
  self->is_touch = FALSE;
  self->last_y = y;

  update (self, TRUE);
}

static void
enter_cb (EditorFullscreenBox *self,
          double               x,
          double               y)
{
  motion_cb (self, x, y);
}

static void
press_cb (EditorFullscreenBox *self,
          int                  n_press,
          double               x,
          double               y,
          GtkGesture          *gesture)
{
  gtk_gesture_set_state (gesture, GTK_EVENT_SEQUENCE_DENIED);

  self->is_touch = TRUE;

  if (y > get_titlebar_area_height (self))
    update (self, TRUE);
}

static void
set_focus (EditorFullscreenBox *self,
           GtkWidget           *widget)
{
  self->last_focus = widget;

  update (self, TRUE);
}

static void
notify_focus_cb (EditorFullscreenBox *self,
                 GParamSpec          *pspec,
                 GtkRoot             *root)
{
  set_focus (self, gtk_root_get_focus (root));
}

static void
notify_reveal_cb (EditorFullscreenBox *self)
{
  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_REVEALED]);
}

static void
editor_fullscreen_box_root (GtkWidget *widget)
{
  EditorFullscreenBox *self = EDITOR_FULLSCREEN_BOX (widget);
  GtkRoot *root;

  GTK_WIDGET_CLASS (editor_fullscreen_box_parent_class)->root (widget);

  root = gtk_widget_get_root (widget);

  if (root && GTK_IS_WINDOW (root)) {
    g_signal_connect_object (root, "notify::focus-widget",
                             G_CALLBACK (notify_focus_cb), widget,
                             G_CONNECT_SWAPPED);

    set_focus (self, gtk_window_get_focus (GTK_WINDOW (root)));
  } else {
    set_focus (self, NULL);
  }
}

static void
editor_fullscreen_box_unroot (GtkWidget *widget)
{
  EditorFullscreenBox *self = EDITOR_FULLSCREEN_BOX (widget);
  GtkRoot *root = gtk_widget_get_root (widget);

  if (root && GTK_IS_WINDOW (root))
    g_signal_handlers_disconnect_by_func (root, notify_focus_cb, widget);

  set_focus (self, NULL);

  GTK_WIDGET_CLASS (editor_fullscreen_box_parent_class)->unroot (widget);
}

static void
editor_fullscreen_box_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  EditorFullscreenBox *self = EDITOR_FULLSCREEN_BOX (object);

  switch (prop_id) {
    case PROP_FULLSCREEN:
      g_value_set_boolean (value, editor_fullscreen_box_get_fullscreen (self));
      break;

    case PROP_AUTOHIDE:
      g_value_set_boolean (value, editor_fullscreen_box_get_autohide (self));
      break;

    case PROP_CONTENT:
      g_value_set_object (value, editor_fullscreen_box_get_content (self));
      break;

    case PROP_REVEALED:
      g_value_set_boolean (value, adw_toolbar_view_get_reveal_top_bars (self->toolbar_view));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
editor_fullscreen_box_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  EditorFullscreenBox *self = EDITOR_FULLSCREEN_BOX (object);

  switch (prop_id) {
    case PROP_FULLSCREEN:
      editor_fullscreen_box_set_fullscreen (self, g_value_get_boolean (value));
      break;

    case PROP_AUTOHIDE:
      editor_fullscreen_box_set_autohide (self, g_value_get_boolean (value));
      break;

    case PROP_CONTENT:
      editor_fullscreen_box_set_content (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
  }
}

static void
editor_fullscreen_box_dispose (GObject *object)
{
  EditorFullscreenBox *self = EDITOR_FULLSCREEN_BOX (object);

  if (self->toolbar_view) {
    gtk_widget_unparent (GTK_WIDGET (self->toolbar_view));
    self->toolbar_view = NULL;
  }

  g_clear_pointer (&self->headers, g_list_free);
  g_clear_handle_id (&self->timeout_id, g_source_remove);
  g_clear_handle_id (&self->forced_reveal_id, g_source_remove);

  G_OBJECT_CLASS (editor_fullscreen_box_parent_class)->dispose (object);
}

static void
editor_fullscreen_box_class_init (EditorFullscreenBoxClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = editor_fullscreen_box_get_property;
  object_class->set_property = editor_fullscreen_box_set_property;
  object_class->dispose = editor_fullscreen_box_dispose;

  widget_class->root = editor_fullscreen_box_root;
  widget_class->unroot = editor_fullscreen_box_unroot;

  props[PROP_FULLSCREEN] =
    g_param_spec_boolean ("fullscreen",
                          NULL, NULL,
                          FALSE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_AUTOHIDE] =
    g_param_spec_boolean ("autohide",
                          NULL, NULL,
                          TRUE,
                          G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_CONTENT] =
    g_param_spec_object ("content",
                         NULL, NULL,
                         GTK_TYPE_WIDGET,
                         G_PARAM_READWRITE | G_PARAM_EXPLICIT_NOTIFY | G_PARAM_STATIC_STRINGS);

  props[PROP_REVEALED] =
    g_param_spec_boolean ("revealed",
                          NULL, NULL,
                          TRUE,
                          G_PARAM_READABLE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (object_class, LAST_PROP, props);

  gtk_widget_class_set_css_name (widget_class, "fullscreenbox");
  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
}

static void
editor_fullscreen_box_init (EditorFullscreenBox *self)
{
  AdwToolbarView *toolbar_view;
  GtkEventController *controller;
  GtkGesture *gesture;

  self->autohide = TRUE;

  toolbar_view = ADW_TOOLBAR_VIEW (adw_toolbar_view_new ());
  adw_toolbar_view_set_top_bar_style (toolbar_view, ADW_TOOLBAR_RAISED);
  adw_toolbar_view_set_bottom_bar_style (toolbar_view, ADW_TOOLBAR_RAISED);

  g_signal_connect_object (toolbar_view, "notify::reveal-top-bars",
                           G_CALLBACK (notify_reveal_cb), self, G_CONNECT_SWAPPED);

  gtk_widget_set_parent (GTK_WIDGET (toolbar_view), GTK_WIDGET (self));
  self->toolbar_view = toolbar_view;

  controller = gtk_event_controller_motion_new ();
  gtk_event_controller_set_propagation_phase (controller, GTK_PHASE_CAPTURE);
  g_signal_connect_object (controller, "enter",
                           G_CALLBACK (enter_cb), self, G_CONNECT_SWAPPED);
  g_signal_connect_object (controller, "motion",
                           G_CALLBACK (motion_cb), self, G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self), controller);

  gesture = gtk_gesture_click_new ();
  gtk_event_controller_set_propagation_phase (GTK_EVENT_CONTROLLER (gesture),
                                              GTK_PHASE_CAPTURE);
  gtk_gesture_single_set_touch_only (GTK_GESTURE_SINGLE (gesture), TRUE);
  g_signal_connect_object (gesture, "pressed",
                           G_CALLBACK (press_cb), self, G_CONNECT_SWAPPED);
  gtk_widget_add_controller (GTK_WIDGET (self), GTK_EVENT_CONTROLLER (gesture));
}

EditorFullscreenBox *
editor_fullscreen_box_new (void)
{
  return g_object_new (EDITOR_TYPE_FULLSCREEN_BOX, NULL);
}

gboolean
editor_fullscreen_box_get_fullscreen (EditorFullscreenBox *self)
{
  g_return_val_if_fail (EDITOR_IS_FULLSCREEN_BOX (self), FALSE);

  return self->fullscreen;
}

void
editor_fullscreen_box_set_fullscreen (EditorFullscreenBox *self,
                                      gboolean             fullscreen)
{
  g_return_if_fail (EDITOR_IS_FULLSCREEN_BOX (self));

  fullscreen = !!fullscreen;

  if (fullscreen == self->fullscreen)
    return;

  self->fullscreen = fullscreen;

  if (!self->autohide)
    return;

  adw_toolbar_view_set_extend_content_to_top_edge (self->toolbar_view, fullscreen);

  if (fullscreen)
    update (self, FALSE);
  else
    show_ui (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_FULLSCREEN]);
}

gboolean
editor_fullscreen_box_get_autohide (EditorFullscreenBox *self)
{
  g_return_val_if_fail (EDITOR_IS_FULLSCREEN_BOX (self), FALSE);

  return self->autohide;
}

void
editor_fullscreen_box_set_autohide (EditorFullscreenBox *self,
                                    gboolean             autohide)
{
  g_return_if_fail (EDITOR_IS_FULLSCREEN_BOX (self));

  autohide = !!autohide;

  if (autohide == self->autohide)
    return;

  self->autohide = autohide;

  if (!self->fullscreen)
    return;

  if (autohide)
    hide_ui (self);
  else
    show_ui (self);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_AUTOHIDE]);
}

GtkWidget *
editor_fullscreen_box_get_content (EditorFullscreenBox *self)
{
  g_return_val_if_fail (EDITOR_IS_FULLSCREEN_BOX (self), NULL);

  return adw_toolbar_view_get_content (self->toolbar_view);
}

void
editor_fullscreen_box_set_content (EditorFullscreenBox *self,
                                   GtkWidget           *content)
{
  g_return_if_fail (EDITOR_IS_FULLSCREEN_BOX (self));
  g_return_if_fail (content == NULL || GTK_IS_WIDGET (content));

  if (editor_fullscreen_box_get_content (self) == content)
    return;

  adw_toolbar_view_set_content (self->toolbar_view, content);

  g_object_notify_by_pspec (G_OBJECT (self), props[PROP_CONTENT]);
}

void
editor_fullscreen_box_add_top_bar (EditorFullscreenBox *self,
                                   GtkWidget           *child)
{
  g_return_if_fail (EDITOR_IS_FULLSCREEN_BOX (self));
  g_return_if_fail (GTK_IS_WIDGET (child));

  adw_toolbar_view_add_top_bar (self->toolbar_view, child);

  self->headers = g_list_prepend (self->headers, child);
}

void
editor_fullscreen_box_add_bottom_bar (EditorFullscreenBox *self,
                                      GtkWidget           *child)
{
  g_return_if_fail (EDITOR_IS_FULLSCREEN_BOX (self));
  g_return_if_fail (GTK_IS_WIDGET (child));

  adw_toolbar_view_add_bottom_bar (self->toolbar_view, child);
}

static void
editor_fullscreen_box_add_child (GtkBuildable *buildable,
                                 GtkBuilder   *builder,
                                 GObject      *object,
                                 const char   *name)
{
  EditorFullscreenBox *self = (EditorFullscreenBox *)buildable;

  g_assert (EDITOR_IS_FULLSCREEN_BOX (self));
  g_assert (GTK_IS_BUILDER (builder));
  g_assert (G_IS_OBJECT (object));

  if (g_strcmp0 (name, "top") == 0 && GTK_IS_WIDGET (object))
    editor_fullscreen_box_add_top_bar (self, GTK_WIDGET (object));
  else if (g_strcmp0 (name, "bottom") == 0 && GTK_IS_WIDGET (object))
    editor_fullscreen_box_add_bottom_bar (self, GTK_WIDGET (object));
  else
    parent_buildable->add_child (buildable, builder, object, name);
}

static void
buildable_iface_init (GtkBuildableIface *iface)
{
  parent_buildable = g_type_interface_peek_parent (iface);
  iface->add_child = editor_fullscreen_box_add_child;
}

static void
clear_forced_timeout (gpointer data)
{
  EditorFullscreenBox *self = data;

  self->forced_reveal_id = 0;

  update (self, TRUE);
}

void
editor_fullscreen_box_reveal (EditorFullscreenBox *self)
{
  g_return_if_fail (EDITOR_IS_FULLSCREEN_BOX (self));

  g_clear_handle_id (&self->forced_reveal_id, g_source_remove);

  show_ui (self);

  g_clear_handle_id (&self->timeout_id, g_source_remove);
  self->forced_reveal_id = g_timeout_add_once (FORCED_REVEAL_DELAY,
                                               (GSourceOnceFunc)clear_forced_timeout,
                                               self);
}
