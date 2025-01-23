/* editor-position-label.c
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

#define G_LOG_DOMAIN "editor-position-label.h"

#include "config.h"

#include "editor-position-label-private.h"

struct _EditorPositionLabel
{
  GtkWidget parent_instance;

  GtkWidget *box;
  GtkLabel *column;
  GtkLabel *line;
};

G_DEFINE_TYPE (EditorPositionLabel, editor_position_label, GTK_TYPE_WIDGET)

static void
editor_position_label_dispose (GObject *object)
{
  EditorPositionLabel *self = (EditorPositionLabel *)object;

  g_clear_pointer (&self->box, gtk_widget_unparent);

  G_OBJECT_CLASS (editor_position_label_parent_class)->dispose (object);
}

static void
editor_position_label_class_init (EditorPositionLabelClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_position_label_dispose;

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-position-label.ui");
  gtk_widget_class_set_css_name (widget_class, "positionlabel");
  gtk_widget_class_bind_template_child (widget_class, EditorPositionLabel, box);
  gtk_widget_class_bind_template_child (widget_class, EditorPositionLabel, column);
  gtk_widget_class_bind_template_child (widget_class, EditorPositionLabel, line);
}

static void
editor_position_label_init (EditorPositionLabel *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));
}

void
_editor_position_label_set_position (EditorPositionLabel *self,
                                     guint                line,
                                     guint                column)
{
  gchar str[32];

  g_return_if_fail (EDITOR_IS_POSITION_LABEL (self));

  g_snprintf (str, sizeof str, "%u, ", line);
  gtk_label_set_label (self->line, str);

  g_snprintf (str, sizeof str, "%u", column);
  gtk_label_set_label (self->column, str);
}

EditorPositionLabel *
_editor_position_label_new (void)
{
  return g_object_new (EDITOR_TYPE_POSITION_LABEL,
                       "visible", TRUE,
                       NULL);
}
