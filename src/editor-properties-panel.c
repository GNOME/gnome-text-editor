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

#include "editor-document-statistics.h"
#include "editor-encoding-model.h"
#include "editor-file-manager.h"
#include "editor-indent-model.h"
#include "editor-language-dialog.h"
#include "editor-newline-model.h"
#include "editor-properties-panel.h"
#include "editor-path-private.h"

struct _EditorPropertiesPanel
{
  GtkWidget parent_instance;

  /* Owned References */
  EditorDocument *document;

  /* Template Objects */
  EditorDocumentStatistics *statistics;
};

enum {
  PROP_0,
  PROP_DOCUMENT,
  N_PROPS
};

G_DEFINE_FINAL_TYPE (EditorPropertiesPanel, editor_properties_panel, GTK_TYPE_WIDGET)

static GParamSpec *properties[N_PROPS];

static char *
file_to_name (gpointer  instance,
              GFile    *file)
{
  g_assert (!file || G_IS_FILE (file));

  if (file == NULL)
    return g_strdup ("—");

  return g_file_get_basename (file);
}

static char *
file_to_location (gpointer instance,
                  GFile    *file)
{
  g_autoptr(GFile) parent = NULL;

  g_assert (!file || G_IS_FILE (file));

  if (file == NULL)
    return g_strdup (_("Draft"));

  if (!(parent = g_file_get_parent (file)))
    return NULL;

  if (g_file_is_native (parent))
    return _editor_path_collapse (g_file_peek_path (parent));
  else
    return g_file_get_uri (parent);
}

static void
open_folder_action (GtkWidget  *widget,
                    const char *action,
                    GVariant   *param)
{
  EditorPropertiesPanel *self = (EditorPropertiesPanel *)widget;
  GFile *file = NULL;

  g_assert (EDITOR_IS_PROPERTIES_PANEL (self));

  if (self->document != NULL &&
      (file = editor_document_get_file (self->document)))
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

  g_clear_object (&self->document);

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
    case PROP_DOCUMENT:
      g_value_set_object (value, editor_properties_panel_get_document (self));
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
    case PROP_DOCUMENT:
      editor_properties_panel_set_document (self, g_value_get_object (value));
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

  properties[PROP_DOCUMENT] =
    g_param_spec_object ("document", NULL, NULL,
                         EDITOR_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE |
                          G_PARAM_EXPLICIT_NOTIFY |
                          G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-properties-panel.ui");

  gtk_widget_class_bind_template_child (widget_class, EditorPropertiesPanel, statistics);

  gtk_widget_class_bind_template_callback (widget_class, file_to_location);
  gtk_widget_class_bind_template_callback (widget_class, file_to_name);

  gtk_widget_class_install_action (widget_class, "open-folder", NULL, open_folder_action);

  g_type_ensure (EDITOR_TYPE_DOCUMENT_STATISTICS);
  g_type_ensure (EDITOR_TYPE_ENCODING);
  g_type_ensure (EDITOR_TYPE_ENCODING_MODEL);
  g_type_ensure (EDITOR_TYPE_INDENT);
  g_type_ensure (EDITOR_TYPE_INDENT_MODEL);
  g_type_ensure (EDITOR_TYPE_NEWLINE);
  g_type_ensure (EDITOR_TYPE_NEWLINE_MODEL);
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
editor_properties_panel_set_document (EditorPropertiesPanel *self,
                                      EditorDocument        *document)
{
  if (g_set_object (&self->document, document))
    {
      editor_document_statistics_set_document (self->statistics, document);
      g_object_notify_by_pspec (G_OBJECT (self), properties[PROP_DOCUMENT]);
    }
}

EditorDocument *
editor_properties_panel_get_document (EditorPropertiesPanel *self)
{
  g_return_val_if_fail (EDITOR_IS_PROPERTIES_PANEL (self), NULL);

  return self->document;
}
