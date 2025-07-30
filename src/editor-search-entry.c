/* editor-search-entry.c
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

#include "config.h"

#include <glib/gi18n.h>

#include "editor-search-entry-private.h"

struct _EditorSearchEntry
{
  GtkWidget  parent_instance;

  GtkImage  *clear;
  GtkText   *text;
  GtkLabel  *info;

  guint      occurrence_count;
  int        occurrence_position;
};

static void editable_iface_init (GtkEditableInterface *iface);

G_DEFINE_TYPE_WITH_CODE (EditorSearchEntry, editor_search_entry, GTK_TYPE_WIDGET,
                         G_IMPLEMENT_INTERFACE (GTK_TYPE_EDITABLE, editable_iface_init))

GtkWidget *
editor_search_entry_new (void)
{
  return g_object_new (EDITOR_TYPE_SEARCH_ENTRY, NULL);
}

static gboolean
editor_search_entry_grab_focus (GtkWidget *widget)
{
  return gtk_widget_grab_focus (GTK_WIDGET (EDITOR_SEARCH_ENTRY (widget)->text));
}

static void
on_text_activate_cb (EditorSearchEntry *self,
                     GtkText           *text)
{
  g_assert (EDITOR_IS_SEARCH_ENTRY (self));
  g_assert (GTK_IS_TEXT (text));

  gtk_widget_activate_action (GTK_WIDGET (self), "search.activate", NULL);
}

static void
on_text_clear_button_pressed_cb (GtkGestureClick   *press,
                                 int                n_press,
                                 double             x,
                                 double             y,
                                 EditorSearchEntry *self)
{
  gtk_editable_set_text (GTK_EDITABLE (self), "");
}

static void
on_text_changed_cb (EditorSearchEntry *self,
                    GtkEditable       *editable)
{
  const char *str;

  str = gtk_editable_get_text (GTK_EDITABLE (self));

  if (str == NULL || *str == '\0')
    gtk_widget_set_visible (GTK_WIDGET (self->clear), FALSE);
  else
    gtk_widget_set_visible (GTK_WIDGET (self->clear), TRUE);
}

static void
on_text_notify_cb (EditorSearchEntry *self,
                   GParamSpec        *pspec,
                   GtkText           *text)
{
  GObjectClass *klass;

  g_assert (EDITOR_IS_SEARCH_ENTRY (self));
  g_assert (GTK_IS_TEXT (text));

  klass = G_OBJECT_GET_CLASS (self);
  pspec = g_object_class_find_property (klass, pspec->name);

  if (pspec != NULL)
    g_object_notify_by_pspec (G_OBJECT (self), pspec);
}

static void
editor_search_entry_dispose (GObject *object)
{
  EditorSearchEntry *self = (EditorSearchEntry *)object;
  GtkWidget *child;

  self->text = NULL;
  self->info = NULL;

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  G_OBJECT_CLASS (editor_search_entry_parent_class)->dispose (object);
}

static void
editor_search_entry_get_property (GObject    *object,
                                  guint       prop_id,
                                  GValue     *value,
                                  GParamSpec *pspec)
{
  if (gtk_editable_delegate_get_property (object, prop_id, value, pspec))
    return;

  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
editor_search_entry_set_property (GObject      *object,
                                  guint         prop_id,
                                  const GValue *value,
                                  GParamSpec   *pspec)
{
  if (gtk_editable_delegate_set_property (object, prop_id, value, pspec))
    return;

  G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
}

static void
editor_search_entry_class_init (EditorSearchEntryClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_search_entry_dispose;
  object_class->get_property = editor_search_entry_get_property;
  object_class->set_property = editor_search_entry_set_property;

  widget_class->grab_focus = editor_search_entry_grab_focus;

  gtk_editable_install_properties (object_class, 1);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BOX_LAYOUT);
  gtk_widget_class_set_css_name (widget_class, "entry");
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-search-entry.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorSearchEntry, clear);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchEntry, info);
  gtk_widget_class_bind_template_child (widget_class, EditorSearchEntry, text);
  gtk_widget_class_bind_template_callback (widget_class, on_text_activate_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_text_clear_button_pressed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_text_changed_cb);
  gtk_widget_class_bind_template_callback (widget_class, on_text_notify_cb);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_g, GDK_CONTROL_MASK|GDK_SHIFT_MASK, "search.move-previous", "b", FALSE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_g, GDK_CONTROL_MASK, "search.move-next", "b", FALSE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Down, 0, "search.move-next", "b", FALSE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Up, 0, "search.move-previous", "b", FALSE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Return, 0, "search.move-next", "b", FALSE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_KP_Enter, 0, "search.move-next", "b", FALSE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Return, GDK_SHIFT_MASK, "search.move-previous", "b", FALSE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_KP_Enter, GDK_SHIFT_MASK, "search.move-previous", "b", FALSE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Return, GDK_CONTROL_MASK, "search.move-next", "b", TRUE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_KP_Enter, GDK_CONTROL_MASK, "search.move-next", "b", TRUE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Return, GDK_CONTROL_MASK|GDK_SHIFT_MASK, "search.move-previous", "b", TRUE);
  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_KP_Enter, GDK_CONTROL_MASK|GDK_SHIFT_MASK, "search.move-previous", "b", TRUE);
}

static void
editor_search_entry_init (EditorSearchEntry *self)
{
  self->occurrence_position = -1;

  gtk_widget_init_template (GTK_WIDGET (self));

  gtk_accessible_update_property (GTK_ACCESSIBLE (self->text),
                                  GTK_ACCESSIBLE_PROPERTY_LABEL, _("Search"),
                                  -1);
}

static GtkEditable *
editor_search_entry_get_delegate (GtkEditable *editable)
{
  return GTK_EDITABLE (EDITOR_SEARCH_ENTRY (editable)->text);
}

static void
editable_iface_init (GtkEditableInterface *iface)
{
  iface->get_delegate = editor_search_entry_get_delegate;
}

static void
editor_search_entry_update_position (EditorSearchEntry *self)
{
  g_assert (EDITOR_IS_SEARCH_ENTRY (self));

  if (self->occurrence_count == 0)
    {
      gtk_label_set_label (self->info, NULL);
    }
  else
    {
      /* translators: the first %u is replaced with the current position, the second with the number of search results */
      g_autofree char *str = g_strdup_printf (_("%u of %u"), MAX (0, self->occurrence_position), self->occurrence_count);
      gtk_label_set_label (self->info, str);
    }
}

int
editor_search_entry_get_occurrence_position (EditorSearchEntry *self)
{
  g_return_val_if_fail (EDITOR_IS_SEARCH_ENTRY (self), -1);

  return self->occurrence_position;
}

void
editor_search_entry_set_occurrence_count (EditorSearchEntry *self,
                                          guint              occurrence_count)
{
  g_assert (EDITOR_IS_SEARCH_ENTRY (self));

  if (self->occurrence_count != occurrence_count)
    {
      self->occurrence_count = occurrence_count;
      editor_search_entry_update_position (self);
    }
}

void
editor_search_entry_set_occurrence_position (EditorSearchEntry *self,
                                             int                occurrence_position)
{
  g_assert (EDITOR_IS_SEARCH_ENTRY (self));

  occurrence_position = MAX (-1, occurrence_position);

  if (self->occurrence_position != occurrence_position)
    {
      self->occurrence_position = occurrence_position;
      editor_search_entry_update_position (self);
    }
}
