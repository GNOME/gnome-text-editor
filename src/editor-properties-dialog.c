/* editor-properties-dialog.c
 *
 * Copyright 2022 Christian Hergert <chergert@redhat.com>
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

#include "editor-document.h"
#include "editor-file-manager.h"
#include "editor-path-private.h"
#include "editor-properties-dialog-private.h"
#include "editor-window.h"

struct _EditorPropertiesDialog
{
  AdwWindow       parent_instance;

  EditorDocument *document;

  AdwActionRow   *all_chars;
  AdwActionRow   *chars;
  AdwActionRow   *lines;
  AdwActionRow   *location;
  AdwActionRow   *name;
  AdwActionRow   *words;
};

G_DEFINE_TYPE (EditorPropertiesDialog, editor_properties_dialog, ADW_TYPE_WINDOW)

enum {
  PROP_0,
  PROP_DOCUMENT,
  N_PROPS
};

static GParamSpec *properties [N_PROPS];

static gboolean
file_to_tooltip (GBinding     *binding,
                 const GValue *from_value,
                 GValue       *to_value,
                 gpointer      user_data)
{
  g_autoptr(GFile) parent = NULL;
  g_autofree char *uri = NULL;
  g_autofree char *title = NULL;
  GFile *file;

  g_assert (G_IS_BINDING (binding));
  g_assert (from_value != NULL);
  g_assert (G_VALUE_HOLDS (from_value, G_TYPE_FILE));

  if (!(file = g_value_get_object (from_value)))
    return TRUE;

  if (!(parent = g_file_get_parent (file)))
    return FALSE;

  uri = g_file_get_uri (file);

  if (g_file_is_native (parent))
    title = _editor_path_collapse (g_file_peek_path (parent));
  else
    title = g_file_get_uri (parent);

  g_value_take_string (to_value, g_steal_pointer (&title));

  return TRUE;
}

static gboolean
file_to_location (GBinding     *binding,
                  const GValue *from_value,
                  GValue       *to_value,
                  gpointer      user_data)
{
  g_autoptr(GFile) parent = NULL;
  g_autofree char *title = NULL;
  GFile *file;

  g_assert (G_IS_BINDING (binding));
  g_assert (from_value != NULL);
  g_assert (G_VALUE_HOLDS (from_value, G_TYPE_FILE));

  if (!(file = g_value_get_object (from_value)))
    {
      g_value_set_string (to_value, _("Draft"));
      return TRUE;
    }

  if (!(parent = g_file_get_parent (file)))
    return FALSE;

  if (g_file_is_native (parent))
    title = _editor_path_collapse (g_file_peek_path (parent));
  else
    title = g_file_get_uri (parent);

  g_value_take_string (to_value, g_steal_pointer (&title));

  return TRUE;
}

/*
 * gedit-docinfo-plugin.c (calculate_info)
 *
 * Copyright (C) 2002-2005 Paolo Maggi
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */
static void
calculate_info (GtkTextBuffer     *doc,
                const GtkTextIter *start,
                const GtkTextIter *end,
                int               *chars,
                int               *words,
                int               *white_chars)
{
  g_autofree char *text = gtk_text_buffer_get_slice (doc, start, end, TRUE);

  *chars = g_utf8_strlen (text, -1);

  if (*chars > 0)
    {
      PangoLogAttr *attrs;
      gint i;

      attrs = g_new0 (PangoLogAttr, *chars + 1);

      pango_get_log_attrs (text,
                           -1,
                           0,
                           pango_language_from_string ("C"),
                           attrs,
                           *chars + 1);

      for (i = 0; i < (*chars); i++)
        {
          if (attrs[i].is_white)
            ++(*white_chars);

          if (attrs[i].is_word_start)
            ++(*words);
        }

      g_free (attrs);
    }
  else
    {
      *white_chars = 0;
      *words = 0;
    }
}

static void
editor_properties_dialog_rescan (EditorPropertiesDialog *self)
{
  GtkTextBuffer *buffer;
  GtkTextIter begin, end;
  int lines = 0;
  int words = 0;
  int chars = 0;
  int white_chars = 0;
  char *str;

  g_assert (EDITOR_IS_PROPERTIES_DIALOG (self));

  if (self->document == NULL)
    return;

  buffer = GTK_TEXT_BUFFER (self->document);

  gtk_text_buffer_get_bounds (buffer, &begin, &end);

  if (gtk_text_iter_equal (&begin, &end))
    lines = 0;
  else
    lines = gtk_text_iter_get_line (&end) - gtk_text_iter_get_line (&begin) + 1;

  calculate_info (buffer,
                  &begin, &end,
                  &chars, &words, &white_chars);

  str = g_strdup_printf("%'d", lines);
  adw_action_row_set_subtitle (self->lines, str);
  g_free (str);

  str = g_strdup_printf("%'d", words);
  adw_action_row_set_subtitle (self->words, str);
  g_free (str);

  str = g_strdup_printf("%'d", chars);
  adw_action_row_set_subtitle (self->all_chars, str);
  g_free (str);

  str = g_strdup_printf("%'d", chars - white_chars);
  adw_action_row_set_subtitle (self->chars, str);
  g_free (str);
}

static void
editor_properties_dialog_save_cb (EditorPropertiesDialog *self,
                                  EditorDocument         *document)
{
  g_assert (EDITOR_IS_PROPERTIES_DIALOG (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  editor_properties_dialog_rescan (self);
}

static void
editor_properties_dialog_set_document (EditorPropertiesDialog *self,
                                       EditorDocument         *document)
{
  g_assert (EDITOR_IS_PROPERTIES_DIALOG (self));
  g_assert (EDITOR_IS_DOCUMENT (document));

  if (g_set_object (&self->document, document))
    {
      GFile *file = editor_document_get_file (document);

      g_object_bind_property (self->document, "title",
                              self->name, "subtitle",
                              G_BINDING_SYNC_CREATE);
      g_object_bind_property (self->document, "title",
                              self->name, "tooltip-text",
                              G_BINDING_SYNC_CREATE);
      g_object_bind_property_full (self->document, "file",
                                   self->location, "subtitle",
                                   G_BINDING_SYNC_CREATE,
                                   file_to_location, NULL, NULL, NULL);
      g_object_bind_property_full (self->document, "file",
                                   self->location, "tooltip-text",
                                   G_BINDING_SYNC_CREATE,
                                   file_to_tooltip, NULL, NULL, NULL);
      gtk_widget_action_set_enabled (GTK_WIDGET (self),
                                     "open-folder",
                                     file != NULL);
      g_signal_connect_object (self->document,
                               "save",
                               G_CALLBACK (editor_properties_dialog_save_cb),
                               self,
                               G_CONNECT_SWAPPED);
      editor_properties_dialog_rescan (self);
    }
}

EditorDocument *
editor_properties_dialog_get_document (EditorPropertiesDialog *self)
{
  g_return_val_if_fail (EDITOR_IS_PROPERTIES_DIALOG (self), NULL);

  return self->document;
}

GtkWidget *
editor_properties_dialog_new (EditorWindow   *window,
                              EditorDocument *document)
{
  g_return_val_if_fail (EDITOR_IS_WINDOW (window), NULL);
  g_return_val_if_fail (EDITOR_IS_DOCUMENT (document), NULL);

  return g_object_new (EDITOR_TYPE_PROPERTIES_DIALOG,
                       "document", document,
                       "transient-for", window,
                       NULL);
}

static void
open_folder_cb (GtkWidget  *widget,
                const char *action_name,
                GVariant   *param)
{
  EditorPropertiesDialog *self = (EditorPropertiesDialog *)widget;
  GFile *file = NULL;

  g_assert (EDITOR_IS_PROPERTIES_DIALOG (self));

  if ((file = editor_document_get_file (self->document)))
    editor_file_manager_show (file, NULL);
}

static void
win_close_cb (GtkWidget  *widget,
              const char *action_name,
              GVariant   *param)
{
  gtk_window_close (GTK_WINDOW (widget));
}

static void
editor_properties_dialog_dispose (GObject *object)
{
  EditorPropertiesDialog *self = (EditorPropertiesDialog *)object;

  g_clear_object (&self->document);

  G_OBJECT_CLASS (editor_properties_dialog_parent_class)->dispose (object);
}

static void
editor_properties_dialog_get_property (GObject    *object,
                                       guint       prop_id,
                                       GValue     *value,
                                       GParamSpec *pspec)
{
  EditorPropertiesDialog *self = EDITOR_PROPERTIES_DIALOG (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      g_value_set_object (value, editor_properties_dialog_get_document (self));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_properties_dialog_set_property (GObject      *object,
                                       guint         prop_id,
                                       const GValue *value,
                                       GParamSpec   *pspec)
{
  EditorPropertiesDialog *self = EDITOR_PROPERTIES_DIALOG (object);

  switch (prop_id)
    {
    case PROP_DOCUMENT:
      editor_properties_dialog_set_document (self, g_value_get_object (value));
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
editor_properties_dialog_class_init (EditorPropertiesDialogClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

  object_class->dispose = editor_properties_dialog_dispose;
  object_class->get_property = editor_properties_dialog_get_property;
  object_class->set_property = editor_properties_dialog_set_property;

  properties [PROP_DOCUMENT] =
    g_param_spec_object ("document",
                         "Document",
                         "Document",
                         EDITOR_TYPE_DOCUMENT,
                         (G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY | G_PARAM_STATIC_STRINGS));

  g_object_class_install_properties (object_class, N_PROPS, properties);

  gtk_widget_class_set_template_from_resource (widget_class, "/org/gnome/TextEditor/ui/editor-properties-dialog.ui");
  gtk_widget_class_bind_template_child (widget_class, EditorPropertiesDialog, all_chars);
  gtk_widget_class_bind_template_child (widget_class, EditorPropertiesDialog, chars);
  gtk_widget_class_bind_template_child (widget_class, EditorPropertiesDialog, lines);
  gtk_widget_class_bind_template_child (widget_class, EditorPropertiesDialog, location);
  gtk_widget_class_bind_template_child (widget_class, EditorPropertiesDialog, name);
  gtk_widget_class_bind_template_child (widget_class, EditorPropertiesDialog, words);

  gtk_widget_class_install_action (widget_class, "open-folder", NULL, open_folder_cb);
  gtk_widget_class_install_action (widget_class, "win.close", NULL, win_close_cb);

  gtk_widget_class_add_binding_action (widget_class, GDK_KEY_Escape, 0, "win.close", NULL);
}

static void
editor_properties_dialog_init (EditorPropertiesDialog *self)
{
  gtk_widget_init_template (GTK_WIDGET (self));

#if DEVELOPMENT_BUILD
  gtk_widget_add_css_class (GTK_WIDGET (self), "devel");
#endif
}
