/* editor-statusbar.c
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

#include "editor-document-private.h"
#include "editor-page-private.h"
#include "editor-statusbar-private.h"

struct _EditorStatusbar
{
  GtkWidget           parent_instance;

  GBindingGroup      *page_bindings;
  GSignalGroup       *document_signals;
  GBindingGroup      *vim_bindings;

  GtkWidget          *box;
  GtkLabel           *command_bar_text;
  GtkLabel           *command_text;
  GtkLabel           *selection_count;
  GtkLabel           *position;
  GtkLabel           *mode;
};

G_DEFINE_FINAL_TYPE (EditorStatusbar, editor_statusbar, GTK_TYPE_WIDGET)

enum {
  PROP_0,
  PROP_COMMAND_TEXT,
  PROP_COMMAND_BAR_TEXT,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

GtkWidget *
editor_statusbar_new (void)
{
  return g_object_new (EDITOR_TYPE_STATUSBAR, NULL);
}

static void
editor_statusbar_cursor_moved_cb (EditorStatusbar *self,
                                  EditorDocument  *document)
{
  GtkTextIter begin, end;
  char str[32];
  int val = 0;

  g_assert (EDITOR_IS_STATUSBAR (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  if (_editor_document_get_loading (document))
    return;

  if (gtk_text_buffer_get_selection_bounds (GTK_TEXT_BUFFER (document), &begin, &end))
    {
      gtk_text_iter_order (&begin, &end);

      if (gtk_text_iter_get_line (&begin) != gtk_text_iter_get_line (&end))
        val = (int)gtk_text_iter_get_line (&end) - (int)gtk_text_iter_get_line (&begin);
      else
        val = (int)gtk_text_iter_get_line_offset (&end) - (int)gtk_text_iter_get_line_offset (&begin);
    }

  if (val == 0)
    str[0] = 0;
  else
    g_snprintf (str, sizeof str, "%d", val);

  gtk_label_set_label (self->selection_count, str);
}

static void
editor_statusbar_dispose (GObject *object)
{
  EditorStatusbar *self = (EditorStatusbar *)object;

  g_clear_object (&self->page_bindings);
  g_clear_object (&self->vim_bindings);
  g_clear_pointer (&self->box, gtk_widget_unparent);

  G_OBJECT_CLASS (editor_statusbar_parent_class)->dispose (object);
}

static void
editor_statusbar_get_property (GObject    *object,
                               guint       prop_id,
                               GValue     *value,
                               GParamSpec *pspec)
{
  EditorStatusbar *self = EDITOR_STATUSBAR (object);

  switch (prop_id)
    {
    case PROP_COMMAND_BAR_TEXT:
      g_value_set_string (value, editor_statusbar_get_command_bar_text (self));
      break;

    case PROP_COMMAND_TEXT:
      g_value_set_string (value, editor_statusbar_get_command_text (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_statusbar_set_property (GObject      *object,
                               guint         prop_id,
                               const GValue *value,
                               GParamSpec   *pspec)
{
  EditorStatusbar *self = EDITOR_STATUSBAR (object);

  switch (prop_id)
    {
    case PROP_COMMAND_BAR_TEXT:
      editor_statusbar_set_command_bar_text (self, g_value_get_string (value));
      break;

    case PROP_COMMAND_TEXT:
      editor_statusbar_set_command_text (self, g_value_get_string (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_statusbar_class_init (EditorStatusbarClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_statusbar_dispose;
  object_class->get_property = editor_statusbar_get_property;
  object_class->set_property = editor_statusbar_set_property;

  properties [PROP_COMMAND_BAR_TEXT] =
    g_param_spec_string ("command-bar-text",
                         "Command Bar Text",
                         "Command Bar Text",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  properties [PROP_COMMAND_TEXT] =
    g_param_spec_string ("command-text",
                         "Command Text",
                         "Command Text",
                         NULL,
                         (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_layout_manager_type (widget_class, GTK_TYPE_BIN_LAYOUT);
  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-statusbar.ui");
  gtk_widget_class_set_css_name (widget_class, "box");
  gtk_widget_class_bind_template_child (widget_class, EditorStatusbar, box);
  gtk_widget_class_bind_template_child (widget_class, EditorStatusbar, command_bar_text);
  gtk_widget_class_bind_template_child (widget_class, EditorStatusbar, command_text);
  gtk_widget_class_bind_template_child (widget_class, EditorStatusbar, mode);
  gtk_widget_class_bind_template_child (widget_class, EditorStatusbar, position);
  gtk_widget_class_bind_template_child (widget_class, EditorStatusbar, selection_count);
}

static void
editor_statusbar_init (EditorStatusbar *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

  self->vim_bindings = g_binding_group_new ();
  g_binding_group_bind (self->vim_bindings, "command-bar-text",
                        self, "command-bar-text",
                        G_BINDING_SYNC_CREATE);
  g_binding_group_bind (self->vim_bindings, "command-text",
                        self, "command-text",
                        G_BINDING_SYNC_CREATE);

  self->page_bindings = g_binding_group_new ();
  g_binding_group_bind (self->page_bindings, "position-label",
                        self->position, "label",
                        G_BINDING_SYNC_CREATE);

  self->document_signals = g_signal_group_new (EDITOR_TYPE_DOCUMENT);
  g_signal_group_connect_object (self->document_signals,
                                 "cursor-moved",
                                 G_CALLBACK (editor_statusbar_cursor_moved_cb),
                                 self,
                                 G_CONNECT_SWAPPED);
}

const char *
editor_statusbar_get_command_bar_text (EditorStatusbar *self)
{
  g_return_val_if_fail (EDITOR_IS_STATUSBAR (self), NULL);

  return gtk_label_get_label (self->command_bar_text);
}

const char *
editor_statusbar_get_command_text (EditorStatusbar *self)
{
  g_return_val_if_fail (EDITOR_IS_STATUSBAR (self), NULL);

  return gtk_label_get_label (self->command_text);
}

void
editor_statusbar_set_command_bar_text (EditorStatusbar *self,
                                       const char      *command_bar_text)
{
  g_return_if_fail (EDITOR_IS_STATUSBAR (self));

  gtk_label_set_label (self->command_bar_text, command_bar_text);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_COMMAND_BAR_TEXT]);
}

void
editor_statusbar_set_command_text (EditorStatusbar *self,
                                   const char      *command_text)
{
  g_return_if_fail (EDITOR_IS_STATUSBAR (self));

  gtk_label_set_label (self->command_text, command_text);
  g_object_notify_by_pspec (G_OBJECT (self), properties [PROP_COMMAND_TEXT]);
}

void
editor_statusbar_set_selection_count (EditorStatusbar *self,
                                      guint            selection_count)
{
  char str[12];

  g_return_if_fail (EDITOR_IS_STATUSBAR (self));

  if (selection_count)
    g_snprintf (str, sizeof str, "%u", selection_count);
  else
    str[0] = 0;

  gtk_label_set_label (self->selection_count, str);
}

void
editor_statusbar_set_position (EditorStatusbar *self,
                               guint            line,
                               guint            line_offset)
{
  char str[32];

  g_return_if_fail (EDITOR_IS_STATUSBAR (self));

  g_snprintf (str, sizeof str, "%u,%u", line, line_offset);
  gtk_label_set_label (self->position, str);
}

void
editor_statusbar_set_overwrite (EditorStatusbar *self,
                                gboolean         overwrite)
{
  g_return_if_fail (EDITOR_IS_STATUSBAR (self));

  if (overwrite)
    gtk_label_set_label (self->mode, "OVR");
  else
    gtk_label_set_label (self->mode, NULL);
}

void
editor_statusbar_bind_page (EditorStatusbar *self,
                            EditorPage      *page)
{
  GtkIMContext *vim = NULL;
  EditorDocument *document = NULL;

  g_return_if_fail (EDITOR_IS_STATUSBAR (self));

  gtk_label_set_label (self->command_bar_text, NULL);
  gtk_label_set_label (self->command_text, NULL);
  gtk_label_set_label (self->mode, NULL);
  gtk_label_set_label (self->position, NULL);
  gtk_label_set_label (self->selection_count, NULL);

  if (page != NULL)
    {
      if (page->vim != NULL)
        vim = gtk_event_controller_key_get_im_context (GTK_EVENT_CONTROLLER_KEY (page->vim));
      document = page->document;
    }

  g_binding_group_set_source (self->page_bindings, page);
  g_binding_group_set_source (self->vim_bindings, vim);
  g_signal_group_set_target (self->document_signals, document);

  if (document != NULL)
    editor_statusbar_cursor_moved_cb (self, document);
}
