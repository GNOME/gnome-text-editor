/* editor-menu-view.c
 *
 * Copyright 2026 Christian Hergert <christian@sourceandstack.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 3 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include "config.h"

#include "editor-menu-view.h"

#define EDITOR_TYPE_MENU_PAGE (editor_menu_page_get_type())
#define EDITOR_TYPE_MENU_BUTTON_ROW (editor_menu_button_row_get_type())

G_DECLARE_FINAL_TYPE (EditorMenuPage, editor_menu_page, EDITOR, MENU_PAGE, AdwNavigationPage)

struct _EditorMenuPage
{
  AdwNavigationPage               parent_instance;
  GMenuModel                     *menu_model;
  AdwNavigationView              *navigation_view;
  GtkWidget                      *scrolled_window;
  GtkWidget                      *box;
  GPtrArray                      *model_watches;
  EditorMenuViewCreateCustomFunc  create_custom;
  gpointer                        create_custom_data;
  guint                           show_header_bar : 1;
};

typedef struct _EditorMenuPageModelWatch
{
  GMenuModel *model;
  gulong      items_changed_handler;
} EditorMenuPageModelWatch;

G_DEFINE_FINAL_TYPE (EditorMenuPage, editor_menu_page, ADW_TYPE_NAVIGATION_PAGE)

G_DECLARE_FINAL_TYPE (EditorMenuButtonRow, editor_menu_button_row, EDITOR, MENU_BUTTON_ROW, AdwActionRow)

struct _EditorMenuButtonRow
{
  AdwActionRow       parent_instance;
  char              *action_name;
  GVariant          *action_target;
  GMenuModel        *submenu;
  AdwNavigationView *navigation_view;
};

G_DEFINE_FINAL_TYPE (EditorMenuButtonRow, editor_menu_button_row, ADW_TYPE_ACTION_ROW)

struct _EditorMenuView
{
  GtkWidget                       parent_instance;
  GMenuModel                     *menu_model;
  AdwNavigationView              *navigation_view;
  char                           *title;
  EditorMenuViewCreateCustomFunc  create_custom;
  gpointer                        create_custom_data;
  gulong                          items_changed_handler;
  gulong                          visible_page_handler;
};

G_DEFINE_FINAL_TYPE (EditorMenuView, editor_menu_view, GTK_TYPE_WIDGET)

enum
{
  PROP_0,
  PROP_MENU_MODEL,
  PROP_TITLE,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

static GtkWidget      *editor_menu_button_row_new        (const char                     *label,
                                                          GIcon                          *icon,
                                                          gboolean                        use_markup,
                                                          const char                     *action_name,
                                                          GVariant                       *action_target,
                                                          GMenuModel                     *submenu,
                                                          const char                     *accel,
                                                          gboolean                        use_check_suffix,
                                                          AdwNavigationView              *navigation_view);
static void            editor_menu_page_items_changed_cb (GMenuModel                     *menu_model,
                                                          int                             position,
                                                          int                             removed,
                                                          int                             added,
                                                          EditorMenuPage                 *self);
static EditorMenuPage *editor_menu_page_new              (GMenuModel                     *menu_model,
                                                          const char                     *title,
                                                          AdwNavigationView              *navigation_view,
                                                          gboolean                        show_header_bar,
                                                          EditorMenuViewCreateCustomFunc  create_custom,
                                                          gpointer                        create_custom_data);
static void            editor_menu_page_rebuild          (EditorMenuPage                 *self);

static void
clear_box (GtkWidget *box)
{
  GtkWidget *child;

  g_assert (GTK_IS_WIDGET (box));

  while ((child = gtk_widget_get_first_child (box)))
    gtk_box_remove (GTK_BOX (box), child);
}

static void
editor_menu_page_model_watch_free (gpointer data)
{
  EditorMenuPageModelWatch *watch = data;

  if (watch->model != NULL)
    g_clear_signal_handler (&watch->items_changed_handler, watch->model);

  g_clear_object (&watch->model);
  g_free (watch);
}

static void
editor_menu_page_watch_model (EditorMenuPage *self,
                              GMenuModel     *model)
{
  EditorMenuPageModelWatch *watch;

  g_assert (EDITOR_IS_MENU_PAGE (self));
  g_assert (G_IS_MENU_MODEL (model));

  for (guint i = 0; i < self->model_watches->len; i++)
    {
      watch = g_ptr_array_index (self->model_watches, i);

      if (watch->model == model)
        return;
    }

  watch = g_new0 (EditorMenuPageModelWatch, 1);
  watch->model = g_object_ref (model);
  watch->items_changed_handler =
    g_signal_connect_object (model,
                             "items-changed",
                             G_CALLBACK (editor_menu_page_items_changed_cb),
                             self,
                             0);

  g_ptr_array_add (self->model_watches, watch);
}

static char *
get_item_string (GMenuModel *model,
                 guint       position,
                 const char *attribute)
{
  char *str = NULL;

  g_assert (G_IS_MENU_MODEL (model));
  g_assert (attribute != NULL);

  g_menu_model_get_item_attribute (model, position, attribute, "s", &str);

  return g_steal_pointer (&str);
}

static gboolean
get_item_boolean (GMenuModel *model,
                  guint       position,
                  const char *attribute)
{
  g_autoptr(GVariant) value = NULL;

  g_assert (G_IS_MENU_MODEL (model));
  g_assert (attribute != NULL);

  if (!(value = g_menu_model_get_item_attribute_value (model, position, attribute, NULL)))
    return FALSE;

  if (g_variant_is_of_type (value, G_VARIANT_TYPE_BOOLEAN))
    return g_variant_get_boolean (value);

  if (g_variant_is_of_type (value, G_VARIANT_TYPE_STRING))
    return TRUE;

  return FALSE;
}

static GIcon *
get_item_icon (GMenuModel *model,
               guint       position,
               const char *attribute)
{
  g_autoptr(GVariant) value = NULL;

  g_assert (G_IS_MENU_MODEL (model));
  g_assert (attribute != NULL);

  if ((value = g_menu_model_get_item_attribute_value (model, position, attribute, NULL)))
    return g_icon_deserialize (value);

  return NULL;
}

static GVariant *
get_item_target (GMenuModel *model,
                 guint       position)
{
  g_assert (G_IS_MENU_MODEL (model));

  return g_menu_model_get_item_attribute_value (model,
                                                position,
                                                G_MENU_ATTRIBUTE_TARGET,
                                                NULL);
}

static GtkTextDirection
text_direction_from_string (const char *str)
{
  if ((g_strcmp0 (str, "rtl") == 0))
    return GTK_TEXT_DIR_RTL;

  if ((g_strcmp0 (str, "ltr") == 0))
    return GTK_TEXT_DIR_LTR;

  return GTK_TEXT_DIR_NONE;
}

static GtkWidget *
create_horizontal_button (GMenuModel *model,
                          guint       position,
                          gboolean    circular)
{
  g_autofree char *label = NULL;
  g_autofree char *action_name = NULL;
  g_autoptr(GVariant) action_target = NULL;
  g_autoptr(GIcon) icon = NULL;
  GtkWidget *button;

  g_assert (G_IS_MENU_MODEL (model));

  button = gtk_button_new ();
  label = get_item_string (model, position, G_MENU_ATTRIBUTE_LABEL);
  action_name = get_item_string (model, position, G_MENU_ATTRIBUTE_ACTION);

  if (!(icon = get_item_icon (model, position, "verb-icon")))
    icon = get_item_icon (model, position, "icon");

  if (circular)
    {
      gtk_widget_add_css_class (button, "circular");
      gtk_widget_add_css_class (button, "image-button");
      gtk_widget_set_hexpand (button, TRUE);
      gtk_widget_set_halign (button, GTK_ALIGN_CENTER);
    }
  else
    {
      gtk_widget_add_css_class (button, "flat");
    }

  if (icon != NULL)
    gtk_button_set_child (GTK_BUTTON (button), gtk_image_new_from_gicon (icon));
  else if (label != NULL)
    gtk_button_set_label (GTK_BUTTON (button), label);

  if (label != NULL)
    gtk_accessible_update_property (GTK_ACCESSIBLE (button),
                                    GTK_ACCESSIBLE_PROPERTY_LABEL, label,
                                    -1);

  if (action_name != NULL)
    {
      action_target = get_item_target (model, position);
      gtk_actionable_set_action_name (GTK_ACTIONABLE (button), action_name);
      gtk_actionable_set_action_target_value (GTK_ACTIONABLE (button), action_target);
    }

  return button;
}

static GtkWidget *
create_horizontal_buttons (GMenuModel *model,
                           const char *text_direction,
                           gboolean    circular)
{
  GtkWidget *box;
  guint n_items;

  g_assert (G_IS_MENU_MODEL (model));

  box = gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 0);

  if (!circular)
    {
      gtk_widget_add_css_class (box, "card");
      gtk_widget_add_css_class (box, "linked");
    }

  gtk_widget_add_css_class (box, "horizontal-buttons");
  gtk_box_set_homogeneous (GTK_BOX (box), TRUE);

  if (text_direction != NULL)
    gtk_widget_set_direction (box, text_direction_from_string (text_direction));

  n_items = g_menu_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      GtkWidget *button;

      button = create_horizontal_button (model, i, circular);
      gtk_box_append (GTK_BOX (box), button);
    }

  return box;
}

static void
add_icon_prefix (AdwActionRow *row,
                 GIcon        *icon)
{
  GtkWidget *image;

  g_assert (ADW_IS_ACTION_ROW (row));

  if (icon == NULL)
    return;

  image = gtk_image_new_from_gicon (icon);
  gtk_widget_add_css_class (image, "dimmed");
  adw_action_row_add_prefix (row, image);
}

static void
add_submenu_suffix (AdwActionRow *row)
{
  GtkWidget *image;

  g_assert (ADW_IS_ACTION_ROW (row));

  image = gtk_image_new_from_icon_name ("go-next-symbolic");
  gtk_widget_add_css_class (image, "dimmed");
  adw_action_row_add_suffix (row, image);
}

static void
add_check_suffix (AdwActionRow *row,
                  const char   *action_name,
                  GVariant     *action_target)
{
  GtkWidget *button;

  g_assert (ADW_IS_ACTION_ROW (row));
  g_assert (action_name != NULL);

  button = g_object_new (GTK_TYPE_CHECK_BUTTON,
                         "can-target", FALSE,
                         "focusable", FALSE,
                         "css-classes", (const char * const[]) {
                           "flat", "editor-menu-check", NULL
                         },
                         "action-name", action_name,
                         "action-target", action_target,
                         NULL);

  adw_action_row_add_suffix (row, button);
  adw_action_row_set_activatable_widget (row, button);
}

static void
add_accel_suffix (AdwActionRow *row,
                  const char   *accel)
{
  g_autofree char *label_text = NULL;
  g_autofree char *accessible_text = NULL;
  GdkModifierType modifiers;
  GtkWidget *label;
  guint key;

  g_assert (ADW_IS_ACTION_ROW (row));
  g_assert (accel != NULL);

  if (!gtk_accelerator_parse (accel, &key, &modifiers))
    return;

  label_text = gtk_accelerator_get_label (key, modifiers);
  accessible_text = gtk_accelerator_get_accessible_label (key, modifiers);
  label = g_object_new (GTK_TYPE_LABEL,
                        "label", label_text,
                        "accessible-role", GTK_ACCESSIBLE_ROLE_PRESENTATION,
                        NULL);
  gtk_widget_add_css_class (label, "dimmed");
  gtk_accessible_update_property (GTK_ACCESSIBLE (row),
                                  GTK_ACCESSIBLE_PROPERTY_KEY_SHORTCUTS, accessible_text,
                                  -1);
  adw_action_row_add_suffix (row, label);
}

static void
add_section_from_model (EditorMenuPage *self,
                        GMenuModel     *model,
                        const char     *title,
                        const char     *display_hint,
                        const char     *text_direction)
{
  GtkWidget *group;
  guint n_items;
  guint n_added = 0;

  g_assert (EDITOR_IS_MENU_PAGE (self));
  g_assert (G_IS_MENU_MODEL (model));

  editor_menu_page_watch_model (self, model);

  group = adw_preferences_group_new ();

  if (title != NULL)
    adw_preferences_group_set_title (ADW_PREFERENCES_GROUP (group), title);

  if ((g_strcmp0 (display_hint, "horizontal-buttons") == 0) ||
      (g_strcmp0 (display_hint, "inline-buttons") == 0) ||
      (g_strcmp0 (display_hint, "circular-buttons") == 0))
    {
      GtkWidget *buttons;

      if (g_menu_model_get_n_items (model) == 0)
        return;

      buttons = create_horizontal_buttons (model,
                                           text_direction,
                                           (g_strcmp0 (display_hint, "circular-buttons") == 0));

      if ((g_strcmp0 (display_hint, "inline-buttons") == 0))
        gtk_widget_add_css_class (buttons, "inline-buttons");
      else if ((g_strcmp0 (display_hint, "circular-buttons") == 0))
        gtk_widget_add_css_class (buttons, "circular-buttons");

      adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), buttons);
      gtk_box_append (GTK_BOX (self->box), group);

      return;
    }

  n_items = g_menu_model_get_n_items (model);

  for (guint i = 0; i < n_items; i++)
    {
      g_autofree char *label = NULL;
      g_autofree char *action_name = NULL;
      g_autofree char *accel = NULL;
      g_autofree char *hidden_when = NULL;
      g_autoptr(GIcon) icon = NULL;
      g_autoptr(GVariant) action_target = NULL;
      g_autoptr(GMenuModel) section = NULL;
      g_autoptr(GMenuModel) submenu = NULL;
      GtkWidget *row;
      gboolean use_markup;
      gboolean use_check_suffix;

      if ((section = g_menu_model_get_item_link (model, i, G_MENU_LINK_SECTION)))
        {
          g_autofree char *section_title = NULL;
          g_autofree char *hint = NULL;
          g_autofree char *direction = NULL;

          section_title = get_item_string (model, i, G_MENU_ATTRIBUTE_LABEL);
          hint = get_item_string (model, i, "display-hint");
          direction = get_item_string (model, i, "text-direction");

          add_section_from_model (self, section, section_title, hint, direction);
          continue;
        }

      if (self->create_custom != NULL)
        {
          g_autofree char *custom = get_item_string (model, i, "custom");

          if (custom != NULL)
            {
              GtkWidget *widget = self->create_custom (custom, self->create_custom_data);

              if (widget != NULL)
                {
                  adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), widget);
                  n_added++;
                }

              continue;
            }
        }

      label = get_item_string (model, i, G_MENU_ATTRIBUTE_LABEL);
      action_name = get_item_string (model, i, G_MENU_ATTRIBUTE_ACTION);
      accel = get_item_string (model, i, "accel");
      hidden_when = get_item_string (model, i, "hidden-when");
      icon = get_item_icon (model, i, "icon");
      action_target = get_item_target (model, i);
      submenu = g_menu_model_get_item_link (model, i, G_MENU_LINK_SUBMENU);
      use_markup = get_item_boolean (model, i, "use-markup");
      use_check_suffix = FALSE;

      if (action_name != NULL && submenu == NULL)
        {
          g_autofree char *role = get_item_string (model, i, "role");

          use_check_suffix = (g_strcmp0 (role, "check") == 0) ||
                             (g_strcmp0 (role, "radio") == 0);
        }

      row = editor_menu_button_row_new (label,
                                          icon,
                                          use_markup,
                                          action_name,
                                          action_target,
                                          submenu,
                                          accel,
                                          use_check_suffix,
                                          self->navigation_view);
      if (row == NULL)
        continue;

      if (submenu != NULL)
        add_submenu_suffix (ADW_ACTION_ROW (row));

      if (action_name != NULL)
        {
          GtkWidget *action_tracker = gtk_button_new ();

          gtk_actionable_set_action_name (GTK_ACTIONABLE (action_tracker), action_name);
          gtk_actionable_set_action_target_value (GTK_ACTIONABLE (action_tracker), action_target);
          gtk_widget_set_visible (action_tracker, FALSE);
          adw_action_row_add_suffix (ADW_ACTION_ROW (row), action_tracker);
          g_object_bind_property (action_tracker, "sensitive",
                                  row, "sensitive",
                                  G_BINDING_SYNC_CREATE);

          if (g_strcmp0 (hidden_when, "action-disabled") == 0)
            g_object_bind_property (action_tracker, "sensitive",
                                    row, "visible",
                                    G_BINDING_SYNC_CREATE);
        }

      adw_preferences_group_add (ADW_PREFERENCES_GROUP (group), row);
      n_added++;
    }

  if (n_added > 0)
    {
      gtk_box_append (GTK_BOX (self->box), group);
      return;
    }

  g_object_ref_sink (group);
  g_object_unref (group);
}

static void
editor_menu_button_row_activate_cb (EditorMenuButtonRow *self)
{
  g_assert (EDITOR_IS_MENU_BUTTON_ROW (self));

  if (self->submenu != NULL)
    {
      const char *title = adw_preferences_row_get_title (ADW_PREFERENCES_ROW (self));
      EditorMenuPage *page = editor_menu_page_new (self->submenu,
                                                   title,
                                                   self->navigation_view,
                                                   TRUE,
                                                   NULL,
                                                   NULL);

      adw_navigation_view_push (self->navigation_view, ADW_NAVIGATION_PAGE (page));
    }
  else if (adw_action_row_get_activatable_widget (ADW_ACTION_ROW (self)) == NULL &&
           self->action_name != NULL)
    {
      g_autoptr(AdwBottomSheet) bottom_sheet = NULL;

      bottom_sheet = ADW_BOTTOM_SHEET (gtk_widget_get_ancestor (GTK_WIDGET (self),
                                                                ADW_TYPE_BOTTOM_SHEET));
      if (bottom_sheet != NULL)
        g_object_ref (bottom_sheet);

      gtk_widget_activate_action_variant (GTK_WIDGET (self),
                                          self->action_name,
                                          self->action_target);

      if (bottom_sheet != NULL)
        adw_bottom_sheet_set_open (bottom_sheet, FALSE);
    }
}

static void
editor_menu_button_row_dispose (GObject *object)
{
  EditorMenuButtonRow *self = EDITOR_MENU_BUTTON_ROW (object);

  g_clear_weak_pointer (&self->navigation_view);

  g_clear_object (&self->submenu);

  g_clear_pointer (&self->action_name, g_free);
  g_clear_pointer (&self->action_target, g_variant_unref);

  G_OBJECT_CLASS (editor_menu_button_row_parent_class)->dispose (object);
}

static void
editor_menu_button_row_class_init (EditorMenuButtonRowClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_menu_button_row_dispose;
}

static void
editor_menu_button_row_init (EditorMenuButtonRow *self)
{
  g_signal_connect (self,
                    "activated",
                    G_CALLBACK (editor_menu_button_row_activate_cb),
                    NULL);
}

static void
editor_menu_button_row_set_submenu (EditorMenuButtonRow *self,
                                    GMenuModel          *submenu)
{
  g_return_if_fail (EDITOR_IS_MENU_BUTTON_ROW (self));
  g_return_if_fail (submenu == NULL || G_IS_MENU_MODEL (submenu));

  if (g_set_object (&self->submenu, submenu))
    gtk_widget_set_sensitive (GTK_WIDGET (self),
                              self->submenu || self->action_name);
}

GtkWidget *
editor_menu_button_row_new (const char        *label,
                            GIcon             *icon,
                            gboolean           use_markup,
                            const char        *action_name,
                            GVariant          *action_target,
                            GMenuModel        *submenu,
                            const char        *accel,
                            gboolean           use_check_suffix,
                            AdwNavigationView *navigation_view)
{
  EditorMenuButtonRow *self;

  g_return_val_if_fail (icon == NULL || G_IS_ICON (icon), NULL);
  g_return_val_if_fail (action_target == NULL || g_variant_is_floating (action_target) == FALSE, NULL);
  g_return_val_if_fail (submenu == NULL || G_IS_MENU_MODEL (submenu), NULL);
  g_return_val_if_fail (ADW_IS_NAVIGATION_VIEW (navigation_view), NULL);

  self = g_object_new (EDITOR_TYPE_MENU_BUTTON_ROW,
                       "title", label ? label : "",
                       "use-markup", use_markup,
                       "use-underline", TRUE,
                       NULL);
  self->action_name = g_strdup (action_name);
  self->action_target = action_target ? g_variant_ref (action_target) : NULL;
  g_set_weak_pointer (&self->navigation_view, navigation_view);

  add_icon_prefix (ADW_ACTION_ROW (self), icon);
  editor_menu_button_row_set_submenu (self, submenu);
  if (accel != NULL)
    add_accel_suffix (ADW_ACTION_ROW (self), accel);
  if (use_check_suffix && action_name != NULL)
    add_check_suffix (ADW_ACTION_ROW (self), action_name, action_target);

  gtk_list_box_row_set_activatable (GTK_LIST_BOX_ROW (self), TRUE);

  return GTK_WIDGET (self);
}

static void
editor_menu_page_items_changed_cb (GMenuModel     *menu_model,
                                   int             position,
                                   int             removed,
                                   int             added,
                                   EditorMenuPage *self)
{
  g_assert (G_IS_MENU_MODEL (menu_model));
  g_assert (EDITOR_IS_MENU_PAGE (self));

  editor_menu_page_rebuild (self);
}

static void
editor_menu_page_rebuild (EditorMenuPage *self)
{
  g_assert (EDITOR_IS_MENU_PAGE (self));

  clear_box (self->box);
  g_ptr_array_set_size (self->model_watches, 0);

  if (self->menu_model != NULL)
    add_section_from_model (self, self->menu_model, NULL, NULL, NULL);
}

static void
editor_menu_page_dispose (GObject *object)
{
  EditorMenuPage *self = EDITOR_MENU_PAGE (object);

  g_clear_pointer (&self->model_watches, g_ptr_array_unref);
  g_clear_object (&self->menu_model);
  g_clear_weak_pointer (&self->navigation_view);

  G_OBJECT_CLASS (editor_menu_page_parent_class)->dispose (object);
}

static void
editor_menu_page_class_init (EditorMenuPageClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = editor_menu_page_dispose;
}

static void
editor_menu_page_init (EditorMenuPage *self)
{
  self->model_watches = g_ptr_array_new_with_free_func (editor_menu_page_model_watch_free);
  self->box = g_object_new (GTK_TYPE_BOX,
                            "orientation", GTK_ORIENTATION_VERTICAL,
                            "spacing", 12,
                            "margin-top", 12,
                            "margin-bottom", 12,
                            "margin-start", 12,
                            "margin-end", 12,
                            NULL);
  self->scrolled_window = g_object_new (GTK_TYPE_SCROLLED_WINDOW,
                                        "vexpand", TRUE,
                                        "hscrollbar-policy", GTK_POLICY_NEVER,
                                        "propagate-natural-height", TRUE,
                                        "child", self->box,
                                        NULL);
}

static void
editor_menu_page_setup_child (EditorMenuPage *self,
                              gboolean        show_header_bar)
{
  g_assert (EDITOR_IS_MENU_PAGE (self));

  if (show_header_bar)
    {
      GtkWidget *header_bar;
      GtkWidget *title;
      GtkWidget *toolbar_view;
      const char *page_title;

      toolbar_view = adw_toolbar_view_new ();
      adw_toolbar_view_set_top_bar_style (ADW_TOOLBAR_VIEW (toolbar_view), ADW_TOOLBAR_FLAT);

      header_bar = adw_header_bar_new ();
      adw_header_bar_set_show_start_title_buttons (ADW_HEADER_BAR (header_bar), FALSE);
      adw_header_bar_set_show_end_title_buttons (ADW_HEADER_BAR (header_bar), FALSE);
      page_title = adw_navigation_page_get_title (ADW_NAVIGATION_PAGE (self));
      title = adw_window_title_new (page_title ? page_title : "", NULL);
      adw_header_bar_set_title_widget (ADW_HEADER_BAR (header_bar), title);
      adw_toolbar_view_add_top_bar (ADW_TOOLBAR_VIEW (toolbar_view), header_bar);

      adw_toolbar_view_set_content (ADW_TOOLBAR_VIEW (toolbar_view), self->scrolled_window);
      adw_navigation_page_set_child (ADW_NAVIGATION_PAGE (self), toolbar_view);
    }
  else
    {
      gtk_widget_set_margin_top (GTK_WIDGET (self->scrolled_window), 36);
      adw_navigation_page_set_child (ADW_NAVIGATION_PAGE (self), self->scrolled_window);
    }
}

static EditorMenuPage *
editor_menu_page_new (GMenuModel                     *menu_model,
                      const char                     *title,
                      AdwNavigationView              *navigation_view,
                      gboolean                        show_header_bar,
                      EditorMenuViewCreateCustomFunc  create_custom,
                      gpointer                        create_custom_data)
{
  EditorMenuPage *self;

  g_return_val_if_fail (G_IS_MENU_MODEL (menu_model), NULL);
  g_return_val_if_fail (ADW_IS_NAVIGATION_VIEW (navigation_view), NULL);

  self = g_object_new (EDITOR_TYPE_MENU_PAGE,
                       "title", title ? title : "",
                       NULL);
  self->menu_model = g_object_ref (menu_model);
  self->show_header_bar = !!show_header_bar;
  self->create_custom = create_custom;
  self->create_custom_data = create_custom_data;
  g_set_weak_pointer (&self->navigation_view, navigation_view);
  editor_menu_page_setup_child (self, show_header_bar);

  editor_menu_page_rebuild (self);

  return self;
}

static gboolean
editor_menu_page_get_show_header_bar (EditorMenuPage *self)
{
  g_return_val_if_fail (EDITOR_IS_MENU_PAGE (self), FALSE);

  return self->show_header_bar;
}

static void
editor_menu_view_update_bottom_sheet_handle (EditorMenuView *self)
{
  AdwNavigationPage *visible_page;
  GtkWidget *bottom_sheet;
  gboolean show_drag_handle = TRUE;

  g_assert (EDITOR_IS_MENU_VIEW (self));

  if (!(bottom_sheet = gtk_widget_get_ancestor (GTK_WIDGET (self), ADW_TYPE_BOTTOM_SHEET)))
    return;

  visible_page = adw_navigation_view_get_visible_page (self->navigation_view);

  if (EDITOR_IS_MENU_PAGE (visible_page))
    show_drag_handle = !editor_menu_page_get_show_header_bar (EDITOR_MENU_PAGE (visible_page));

  adw_bottom_sheet_set_show_drag_handle (ADW_BOTTOM_SHEET (bottom_sheet), show_drag_handle);
}

static void
editor_menu_view_visible_page_changed_cb (EditorMenuView    *self,
                                          GParamSpec        *pspec,
                                          AdwNavigationView *navigation_view)
{
  g_assert (ADW_IS_NAVIGATION_VIEW (navigation_view));
  g_assert (EDITOR_IS_MENU_VIEW (self));

  editor_menu_view_update_bottom_sheet_handle (self);
}

static void
editor_menu_view_rebuild (EditorMenuView *self)
{
  AdwNavigationPage *pages[1] = { NULL };

  g_assert (EDITOR_IS_MENU_VIEW (self));

  if (self->menu_model != NULL)
    pages[0] = ADW_NAVIGATION_PAGE (editor_menu_page_new (self->menu_model,
                                                            self->title,
                                                            self->navigation_view,
                                                            FALSE,
                                                            self->create_custom,
                                                            self->create_custom_data));

  adw_navigation_view_replace (self->navigation_view,
                               self->menu_model ? pages : NULL,
                               self->menu_model ? 1 : 0);
}

static void
editor_menu_view_items_changed_cb (EditorMenuView *self,
                                   int             position,
                                   int             removed,
                                   int             added,
                                   GMenuModel     *menu_model)
{
  g_assert (G_IS_MENU_MODEL (menu_model));
  g_assert (EDITOR_IS_MENU_VIEW (self));

  editor_menu_view_rebuild (self);
  editor_menu_view_update_bottom_sheet_handle (self);
}

static void
editor_menu_view_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  EditorMenuView *self = EDITOR_MENU_VIEW (object);

  switch (prop_id)
    {
    case PROP_MENU_MODEL:
      g_value_take_object (value, editor_menu_view_dup_menu_model (self));
      break;

    case PROP_TITLE:
      g_value_set_string (value, self->title);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_menu_view_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  EditorMenuView *self = EDITOR_MENU_VIEW (object);

  switch (prop_id)
    {
    case PROP_MENU_MODEL:
      editor_menu_view_set_menu_model (self, g_value_get_object (value));
      break;

    case PROP_TITLE:
      g_set_str (&self->title, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_menu_view_dispose (GObject *object)
{
  EditorMenuView *self = EDITOR_MENU_VIEW (object);

  if (self->menu_model != NULL && self->items_changed_handler != 0)
    {
      g_clear_signal_handler (&self->items_changed_handler, self->menu_model);
    }

  g_clear_object (&self->menu_model);
  g_clear_signal_handler (&self->visible_page_handler, self->navigation_view);
  if (self->navigation_view != NULL)
    {
      gtk_widget_unparent (GTK_WIDGET (self->navigation_view));
      self->navigation_view = NULL;
    }

  G_OBJECT_CLASS (editor_menu_view_parent_class)->dispose (object);
}

static void
editor_menu_view_class_init (EditorMenuViewClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->get_property = editor_menu_view_get_property;
  object_class->set_property = editor_menu_view_set_property;
  object_class->dispose = editor_menu_view_dispose;

  properties[PROP_MENU_MODEL] =
    g_param_spec_object ("menu-model", NULL, NULL,
                         G_TYPE_MENU_MODEL,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  properties[PROP_TITLE] =
    g_param_spec_string ("title", NULL, NULL,
                         NULL,
                         (G_PARAM_READWRITE |
                          G_PARAM_CONSTRUCT_ONLY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "editormenuview");
}

static void
editor_menu_view_init (EditorMenuView *self)
{
  self->navigation_view = g_object_new (ADW_TYPE_NAVIGATION_VIEW,
                                        "vhomogeneous", TRUE,
                                        NULL);
  self->visible_page_handler =
    g_signal_connect_object (self->navigation_view,
                             "notify::visible-page",
                             G_CALLBACK (editor_menu_view_visible_page_changed_cb),
                             self,
                             G_CONNECT_SWAPPED);
  gtk_widget_set_parent (GTK_WIDGET (self->navigation_view), GTK_WIDGET (self));
}

GtkWidget *
editor_menu_view_new (const char *title)
{
  return g_object_new (EDITOR_TYPE_MENU_VIEW,
                       "title", title,
                       NULL);
}

gboolean
editor_menu_view_pop (EditorMenuView *self)
{
  g_return_val_if_fail (EDITOR_IS_MENU_VIEW (self), FALSE);

  return adw_navigation_view_pop (self->navigation_view);
}

GMenuModel *
editor_menu_view_get_menu_model (EditorMenuView *self)
{
  g_return_val_if_fail (EDITOR_IS_MENU_VIEW (self), NULL);

  return self->menu_model;
}

GMenuModel *
editor_menu_view_dup_menu_model (EditorMenuView *self)
{
  g_return_val_if_fail (EDITOR_IS_MENU_VIEW (self), NULL);

  return self->menu_model ? g_object_ref (self->menu_model) : NULL;
}

void
editor_menu_view_set_menu_model (EditorMenuView *self,
                                 GMenuModel     *menu_model)
{
  g_return_if_fail (EDITOR_IS_MENU_VIEW (self));
  g_return_if_fail (menu_model == NULL || G_IS_MENU_MODEL (menu_model));

  if (self->menu_model == menu_model)
    return;

  if (menu_model != NULL)
    g_object_ref (menu_model);

  if (self->menu_model != NULL)
    {
      g_clear_signal_handler (&self->items_changed_handler, self->menu_model);
      g_clear_object (&self->menu_model);
    }

  self->menu_model = menu_model;

  if (self->menu_model != NULL)
    self->items_changed_handler =
      g_signal_connect_object (self->menu_model,
                               "items-changed",
                               G_CALLBACK (editor_menu_view_items_changed_cb),
                               self,
                               G_CONNECT_SWAPPED);

  editor_menu_view_rebuild (self);
  g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_MENU_MODEL]);
}

void
editor_menu_view_set_create_custom_func (EditorMenuView                 *self,
                                         EditorMenuViewCreateCustomFunc  func,
                                         gpointer                        user_data)
{
  g_return_if_fail (EDITOR_IS_MENU_VIEW (self));

  self->create_custom = func;
  self->create_custom_data = user_data;
  editor_menu_view_rebuild (self);
}
