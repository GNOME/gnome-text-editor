/*
 * editor-properties-panel.c
 *
 * Copyright 2025 Christian Hergert <chergert@redhat.com>
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

#include "editor-document-private.h"
#include "editor-document-statistics.h"
#include "editor-encoding-model.h"
#include "editor-file-manager.h"
#include "editor-indent-model.h"
#include "editor-language-dialog.h"
#include "editor-newline-model.h"
#include "editor-page-private.h"
#include "editor-path-private.h"
#include "editor-properties.h"
#include "editor-properties-panel.h"
#include "editor-source-view.h"
#include "editor-window.h"

struct _EditorPropertiesPanel
{
  GtkWidget parent_instance;

  /* Owned References */
  EditorPage     *page;

  /* Template References */
  EditorProperties *properties;
  GtkImage         *open_image;
};

enum {
  PROP_0,
  PROP_PAGE,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorPropertiesPanel, editor_properties_panel, GTK_TYPE_WIDGET)

static GParamSpec *properties[N_PROPS];

static void
editor_properties_panel_notify_can_open_cb (EditorPropertiesPanel *self)
{
  gboolean can_open;

  g_assert (EDITOR_IS_PROPERTIES_PANEL (self));

  /*
   * Work around some weird styling due to actions being disabled and the
   * subtitle getting grayed out in the properties panel.
   */

  can_open = editor_properties_get_can_open (self->properties);
  gtk_widget_action_set_enabled (GTK_WIDGET (self), "open-folder", can_open);

  if (can_open)
    gtk_widget_remove_css_class (GTK_WIDGET (self->open_image), "dim-label");
  else
    gtk_widget_add_css_class (GTK_WIDGET (self->open_image), "dim-label");
}

static void
open_folder_action (GtkWidget  *widget,
                    const char *action,
                    GVariant   *param)
{
  EditorPropertiesPanel *self = (EditorPropertiesPanel *)widget;
  GFile *file = NULL;

  g_assert (EDITOR_IS_PROPERTIES_PANEL (self));

  if ((file = editor_document_get_file (self->page->document)))
    editor_file_manager_show (file, NULL);
}

static void
editor_properties_panel_dispose (GObject *object)
{
  EditorPropertiesPanel *self = (EditorPropertiesPanel *)object;
  GtkWidget *child;

  gtk_widget_dispose_template (GTK_WIDGET (self), EDITOR_TYPE_PROPERTIES_PANEL);

  while ((child = gtk_widget_get_first_child (GTK_WIDGET (self))))
    gtk_widget_unparent (child);

  g_clear_object (&self->page);

  G_OBJECT_CLASS (editor_properties_panel_parent_class)->dispose (object);
}

static void
editor_properties_panel_get_property (GObject    *object,
                                      guint       prop_id,
                                      GValue     *value,
                                      GParamSpec *pspec)
{
  EditorPropertiesPanel *self = EDITOR_PROPERTIES_PANEL (object);

  switch (prop_id)
    {
    case PROP_PAGE:
      g_value_set_object (value, editor_properties_panel_get_page (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_properties_panel_set_property (GObject      *object,
                                      guint         prop_id,
                                      const GValue *value,
                                      GParamSpec   *pspec)
{
  EditorPropertiesPanel *self = EDITOR_PROPERTIES_PANEL (object);

  switch (prop_id)
    {
    case PROP_PAGE:
      editor_properties_panel_set_page (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_properties_panel_class_init (EditorPropertiesPanelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_properties_panel_dispose;
  object_class->get_property = editor_properties_panel_get_property;
  object_class->set_property = editor_properties_panel_set_property;

  properties[PROP_PAGE] =
    g_param_spec_object ("page", NULL, NULL,
                         EDITOR_TYPE_PAGE,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-properties-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, EditorPropertiesPanel, properties);
  gtk_widget_class_bind_template_child (widget_class, EditorPropertiesPanel, open_image);

  gtk_widget_class_bind_template_callback (widget_class, editor_properties_panel_notify_can_open_cb);

  gtk_widget_class_install_action (widget_class, "open-folder", NULL, open_folder_action);

  g_type_ensure (EDITOR_TYPE_DOCUMENT_STATISTICS);
  g_type_ensure (EDITOR_TYPE_ENCODING);
  g_type_ensure (EDITOR_TYPE_ENCODING_MODEL);
  g_type_ensure (EDITOR_TYPE_INDENT);
  g_type_ensure (EDITOR_TYPE_INDENT_MODEL);
  g_type_ensure (EDITOR_TYPE_NEWLINE);
  g_type_ensure (EDITOR_TYPE_NEWLINE_MODEL);
  g_type_ensure (EDITOR_TYPE_PROPERTIES);
}

static void
editor_properties_panel_init (EditorPropertiesPanel *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

GtkWidget *
editor_properties_panel_new (void)
{
  return g_object_new (EDITOR_TYPE_PROPERTIES_PANEL, NULL);
}

void
editor_properties_panel_set_page (EditorPropertiesPanel *self,
                                  EditorPage            *page)
{
  g_return_if_fail (EDITOR_IS_PROPERTIES_PANEL (self));
  g_return_if_fail (!page || EDITOR_IS_PAGE (page));

  if (g_set_object (&self->page, page))
    {
      editor_properties_set_page (self->properties, page);
      editor_properties_panel_notify_can_open_cb (self);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_PAGE]);
    }
}

EditorPage *
editor_properties_panel_get_page (EditorPropertiesPanel *self)
{
  g_return_val_if_fail (EDITOR_IS_PROPERTIES_PANEL (self), NULL);

  return self->page;
}
